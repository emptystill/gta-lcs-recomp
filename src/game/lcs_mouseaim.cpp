// See lcs_mouseaim.h and docs/LCS.md "Mouse aim".
#include "game/lcs_mouseaim.h"

#include "game/vcs_input.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "render/presenter.h"
#include "runtime/ps2_control.h"
#include "settings/settings.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace lcs::mouseaim
{
    namespace
    {
        constexpr uint32_t kRamMask = 0x01FFFFFFu;
        constexpr uint32_t kPlayerPed = 0x00408E40u; // CWorld::Players[0].m_pPed
        // CPed: the vehicle he is in and the driving flag, the state (m_nPedState), the 64-bit flags (bit 35 AIM_GUN's,
        // 39 aiming the gun, 40 restoring it), the heading now / wanted (m_fRotationCur / Dest), SetAimFlag's angle and
        // its timer, the targets (+0x6E0 look / aim, +0x530 m_pPointGunAt), the aim's pitch for the player (+0xC78),
        // the weapon slots (0x1C each, type first) and the current slot (s8)
        constexpr uint32_t kPedVehicle = 0x534u, kPedDriving = 0x538u, kPedState = 0x34Cu, kPedFlags = 0x190u;
        constexpr uint32_t kPedRotCur = 0x4E0u, kPedRotDest = 0x4E4u, kPedAimAngle = 0x6E4u, kPedAimTimer = 0x6F4u;
        constexpr uint32_t kPedPitch = 0xC78u, kPedWeapons = 0x594u, kPedSlot = 0x6B8u, kSlotSize = 0x1Cu;
        constexpr uint64_t kFlagAimGunState = 0x800000000ull, kFlagAiming = 0x8000000000ull, kFlagRestoring = 0x10000000000ull;
        constexpr uint32_t kStateIdle = 1u, kStateAttack = 0x10u, kStateAimGun = 0x16u;
        constexpr uint32_t kWeaponInfoPtr = 0x003D9B48u; // *(*(this) + 4) + type * 0x70: +0 the fire type
        constexpr uint32_t kWeaponInfoSize = 0x70u;
        constexpr uint32_t kSetPointGunAt = 0x001BA618u; // CPed::SetPointGunAt(ped, entity); entity 0: the pose alone
        constexpr uint32_t kTheCamera = 0x0043BBF0u;     // its matrix: +0x10 forward, +0x30 position
        constexpr uint32_t kActiveCam = 0x10Cu, kCam0 = 0x220u, kCamStride = 0x2B0u, kCamMode = 0x40u;
        constexpr uint32_t kModeFollowPed = 4u;
        constexpr uint32_t kGameClock = 0x003D9B70u; // CTimer::m_snTimeInMilliseconds
        // The weapons' line test (0x2B0BD0; VCS 0x2D0870 by xmatch: from a0, to a1, the shooter t1) and where it returns
        // to in the instant-hit shot (pistol, SMGs, rifles: 0x2A59BC) and in the shotgun's (0x2A93E4)
        constexpr uint32_t kShotLine = 0x002B0BD0u, kInstantHitReturn = 0x002A59BCu, kShotgunReturn = 0x002A93E4u;
        constexpr uint32_t kScratch = 0x0007FF00u; // the ray's start: EE kernel RAM no one uses (above the runtime's
                                                   // heap and pools, 0x40000..0x78000; all zero in play)
        constexpr float kShoulder = 0.6f, kShoulderUp = 0.12f, kCloser = 0.3f; // the camera while aiming

        enum class State
        {
            Idle,
            Aiming,
        };

        uint8_t *g_rdram = nullptr;
        PS2Runtime::RecompiledFunction g_origShotLine = nullptr;
        bool g_installed = false;
        std::atomic<bool> g_want{false};       // the target button held, the setting on, the mouse in use
        std::atomic<bool> g_aiming{false};     // as State, for other threads
        std::atomic<float> g_shift{0.0f};      // cameraShift()
        std::atomic<uint32_t> g_shots{0};
        State g_state = State::Idle;           // game thread
        uint32_t g_lastClock = 0;
        uint32_t g_retryClock = 0;

        uint32_t rd32(uint32_t a)
        {
            uint32_t v;
            std::memcpy(&v, g_rdram + (a & kRamMask), 4);
            return v;
        }
        float rdf(uint32_t a)
        {
            float v;
            std::memcpy(&v, g_rdram + (a & kRamMask), 4);
            return v;
        }
        void wr32(uint32_t a, uint32_t v) { std::memcpy(g_rdram + (a & kRamMask), &v, 4); }
        void wrf(uint32_t a, float v) { std::memcpy(g_rdram + (a & kRamMask), &v, 4); }
        uint64_t rd64(uint32_t a)
        {
            uint64_t v;
            std::memcpy(&v, g_rdram + (a & kRamMask), 8);
            return v;
        }
        void wr64(uint32_t a, uint64_t v) { std::memcpy(g_rdram + (a & kRamMask), &v, 8); }
        bool inRam(uint32_t a) { return a >= 0x00100000u && a < 0x02000000u; }

        bool enabled()
        {
            return g_installed && settings::value("mouse.aim") != 0 && settings::value("mouse.camera") != 0 &&
                   vcs::input::lastDevice() == vcs::input::Device::Keyboard;
        }

        uint32_t player()
        {
            const uint32_t ped = g_rdram ? rd32(kPlayerPed) : 0u;
            return inRam(ped) ? ped : 0u;
        }

        bool onFoot(uint32_t ped) { return !(rd32(ped + kPedDriving) & 0xFFu) && !inRam(rd32(ped + kPedVehicle)); }

        // A gun this aim is for: instant hit (fire type 1) or the flame-thrower (3); not the snipers (types 28 / 29,
        // whose target button opens the scope: camera mode 7, checked weapon by weapon), not melee (0) or thrown (2).
        bool gunInHand(uint32_t ped)
        {
            const int8_t slot = static_cast<int8_t>(g_rdram[(ped + kPedSlot) & kRamMask]);
            if (slot < 0 || slot >= 10)
                return false;
            const uint32_t type = rd32(ped + kPedWeapons + static_cast<uint32_t>(slot) * kSlotSize);
            if (type == 0 || type > 60 || type == 28 || type == 29)
                return false;
            const uint32_t table = rd32(kWeaponInfoPtr);
            if (!inRam(table) || !inRam(rd32(table + 4)))
                return false;
            const uint32_t fire = rd32(rd32(table + 4) + type * kWeaponInfoSize);
            return fire == 1 || fire == 3;
        }

        bool followCamera()
        {
            const uint32_t active = std::min<uint32_t>(g_rdram[kTheCamera + kActiveCam], 2u);
            return (rd32(kTheCamera + kCam0 + active * kCamStride + kCamMode) & 0xFFFFu) == kModeFollowPed;
        }

        // The camera's forward vector (the picture's middle, where the circle is).
        bool cameraForward(float f[3])
        {
            float len = 0.0f;
            for (int i = 0; i < 3; ++i)
            {
                f[i] = rdf(kTheCamera + 0x10u + 4u * i);
                len += f[i] * f[i];
            }
            len = std::sqrt(len);
            if (!(len > 0.5f && len < 1.5f))
                return false;
            for (int i = 0; i < 3; ++i)
                f[i] /= len;
            return true;
        }

        void leave(uint32_t ped, const char *why)
        {
            if (g_state != State::Aiming)
                return;
            g_state = State::Idle;
            g_aiming.store(false, std::memory_order_relaxed);
            if (ped)
            {
                // as letting go of a target does: the gun restored (bit 40), aiming and AIM_GUN's bit off, idle
                uint64_t flags = rd64(ped + kPedFlags);
                flags = (flags & ~(kFlagAiming | kFlagAimGunState)) | kFlagRestoring;
                wr64(ped + kPedFlags, flags);
                if (rd32(ped + kPedState) == kStateAimGun)
                    wr32(ped + kPedState, kStateIdle);
            }
            std::fprintf(stderr, "[mouseaim] aim off (%s)\n", why);
        }

        // The line of one of the player's shots, down the camera's ray through the circle: from the ray's point
        // nearest the gun, as long as the game's own line. The shotgun's pellets keep their spread about it.
        PS2_GUEST_CC void shotLine(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t ra = GPR_U32(ctx, 31);
            const uint32_t ped = player();
            float f[3];
            if (g_aiming.load(std::memory_order_relaxed) && (ra == kInstantHitReturn || ra == kShotgunReturn) && ped &&
                GPR_U32(ctx, 9) == ped && cameraForward(f))
            {
                const uint32_t from = GPR_U32(ctx, 4), to = GPR_U32(ctx, 5);
                float gun[3], end[3], cam[3], d[3], range = 0.0f, along = 0.0f;
                for (int i = 0; i < 3; ++i)
                {
                    gun[i] = rdf(from + 4u * i);
                    end[i] = rdf(to + 4u * i);
                    cam[i] = rdf(kTheCamera + 0x30u + 4u * i);
                    d[i] = end[i] - gun[i];
                    range += d[i] * d[i];
                    along += (gun[i] - cam[i]) * f[i];
                }
                range = std::sqrt(range);
                along = std::max(0.0f, along);
                if (range > 0.01f)
                {
                    float dir[3];
                    if (ra == kShotgunReturn)
                    {
                        // the pellet's own direction turned with the aim: its offset from the gun's aim (the angle /
                        // pitch written, the camera's forward) kept, put about the camera's ray
                        for (int i = 0; i < 3; ++i)
                            dir[i] = d[i] / range;
                    }
                    else
                    {
                        for (int i = 0; i < 3; ++i)
                            dir[i] = f[i];
                    }
                    for (int i = 0; i < 3; ++i)
                    {
                        const float start = cam[i] + f[i] * along;
                        wrf(kScratch + 4u * i, start);
                        wrf(to + 4u * i, start + dir[i] * range);
                    }
                    wrf(kScratch + 12u, 0.0f);
                    SET_GPR_U32(ctx, 4, kScratch);
                    g_shots.fetch_add(1, std::memory_order_relaxed);
                }
            }
            g_origShotLine(rdram, ctx, runtime);
        }
    }

    void install(PS2Runtime &runtime)
    {
        g_rdram = runtime.memory().getRDRAM();
        if (const char *e = std::getenv("LCS_MOUSE_AIM"); e && e[0] == '0')
        {
            std::fprintf(stderr, "[mouseaim] off (LCS_MOUSE_AIM=0)\n");
            return;
        }
        g_origShotLine = runtime.lookupFunction(kShotLine);
        if (!g_origShotLine || !runtime.lookupFunction(kSetPointGunAt))
            return;
        runtime.replaceFunction(kShotLine, &shotLine);
        g_installed = true;
        // aimstate -> ok active=<0|1> shots=<player shots down the camera's ray so far> state=<ped state> (aim checks)
        ps2control::addCommand("aimstate", [](const std::string &, ps2control::Reply reply) {
            const uint32_t ped = player();
            char text[128];
            std::snprintf(text, sizeof(text), "ok active=%d shots=%u state=%u", g_aiming.load() ? 1 : 0, g_shots.load(),
                          ped ? rd32(ped + kPedState) : 0u);
            reply(text);
        });
    }

    vcs::aim::Output update(const vcs::aim::Input &in)
    {
        vcs::aim::Output out;
        const bool want = in.want && enabled();
        g_want.store(want, std::memory_order_relaxed);
        const uint32_t ped = player();
        // the target button is ours while aiming, and while it is held to aim (the game would lock on); the left stick
        // is centred while aiming (he stands)
        if (g_aiming.load(std::memory_order_relaxed) || (want && ped && onFoot(ped) && gunInHand(ped)))
        {
            out.own = true;
            out.r1 = out.l3 = false;
            out.leftStick = g_aiming.load(std::memory_order_relaxed);
        }
        return out;
    }

    bool onFrame(uint8_t *rdram, R5900Context *ctx)
    {
        if (!g_installed || !rdram)
            return false;
        const uint32_t clock = rd32(kGameClock);
        const float dt = std::clamp(static_cast<float>(clock - g_lastClock) / 1000.0f, 0.0f, 0.1f);
        g_lastClock = clock;
        {
            const float target = g_state == State::Aiming ? 1.0f : 0.0f, now = g_shift.load(std::memory_order_relaxed);
            const float k = dt > 0.0f ? 1.0f - std::exp(-dt / 0.12f) : 0.0f;
            g_shift.store(now + (target - now) * k, std::memory_order_relaxed);
        }
        const uint32_t ped = player();
        const bool want = g_want.load(std::memory_order_relaxed);
        if (g_state == State::Aiming)
        {
            const uint32_t state = ped ? rd32(ped + kPedState) : 0u;
            if (!want || !ped || !onFoot(ped) || !gunInHand(ped) || !followCamera())
            {
                leave(ped, !want ? "released" : "no gun / not on foot / another camera");
                return false;
            }
            if (state != kStateAimGun && state != kStateAttack)
            {
                // the game took him out of the pose (hit, a fall, a reload's state): back in after a moment
                if (state == kStateIdle && clock - g_retryClock > 300u)
                {
                    g_retryClock = clock;
                    SET_GPR_U32(ctx, 4, ped);
                    SET_GPR_U32(ctx, 5, 0u);
                    ctx->pc = kSetPointGunAt;
                    return true;
                }
                if (state != kStateIdle)
                {
                    leave(ped, "the game changed his state");
                    return false;
                }
            }
            // where the camera looks: his heading and the gun's (SetAimFlag(angle)'s fields), its pitch
            float f[3];
            if (cameraForward(f))
            {
                const float angle = std::atan2(-f[0], f[1]);
                // the IK's pitch is + down (checked: the camera's own sign raised his arm looking down)
                const float pitch = -std::asin(std::clamp(f[2], -1.0f, 1.0f));
                wrf(ped + kPedAimAngle, angle);
                wrf(ped + kPedPitch, pitch);
                wrf(ped + kPedRotCur, angle);
                wrf(ped + kPedRotDest, angle);
                wr32(ped + kPedAimTimer, 0u);
                const uint64_t flags = rd64(ped + kPedFlags);
                wr64(ped + kPedFlags, (flags & ~kFlagRestoring) | kFlagAiming);
            }
            return false;
        }
        // not aiming: in when the button is held with a gun, on foot, in the follow camera, standing or walking
        if (want && ped && onFoot(ped) && gunInHand(ped) && followCamera())
        {
            const uint32_t state = rd32(ped + kPedState);
            if (state == kStateIdle || state == kStateAttack)
            {
                g_state = State::Aiming;
                g_aiming.store(true, std::memory_order_relaxed);
                g_retryClock = clock;
                std::fprintf(stderr, "[mouseaim] aim on: SetPointGunAt(%08x, 0)\n", ped);
                SET_GPR_U32(ctx, 4, ped);
                SET_GPR_U32(ctx, 5, 0u);
                ctx->pc = kSetPointGunAt; // returns into the limiter (lcs_overrides.cpp frameBoundary)
                return true;
            }
        }
        return false;
    }

    float cameraShift() { return g_shift.load(std::memory_order_relaxed); }

    void drawCrosshair()
    {
        if (!g_aiming.load(std::memory_order_relaxed) || !ImGui::GetCurrentContext())
            return;
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        float x0 = 0, y0 = 0, w = 0, h = 0;
        present::fitRect(present::displayAspect(true), 0.0f, vp->Size.x, vp->Size.y, x0, y0, w, h);
        const float cx = vp->Pos.x + x0 + 0.5f * w, cy = vp->Pos.y + y0 + 0.5f * h;
        // a circle with a dot, as the game's free-aim reticle in VCS: white over a dark outline, sized by the picture
        const float s = std::max(1.0f, h / 1080.0f);
        ImDrawList *draw = ImGui::GetForegroundDrawList();
        draw->AddCircle(ImVec2(cx, cy), 14.0f * s, IM_COL32(0, 0, 0, 160), 40, 4.0f * s);
        draw->AddCircle(ImVec2(cx, cy), 14.0f * s, IM_COL32(255, 255, 255, 220), 40, 2.0f * s);
        draw->AddCircleFilled(ImVec2(cx, cy), 2.6f * s, IM_COL32(0, 0, 0, 160));
        draw->AddCircleFilled(ImVec2(cx, cy), 1.6f * s, IM_COL32(255, 255, 255, 230));
    }
}
