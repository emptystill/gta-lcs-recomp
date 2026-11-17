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
        constexpr uint32_t kWeaponFlags = 0x6Cu, kFlagArmAim = 0x80u;
        constexpr uint32_t kSetPointGunAt = 0x001BA618u; // CPed::SetPointGunAt(ped, entity); entity 0: the pose alone
        constexpr uint32_t kClearPointGunAt = 0x001BAA70u; // CPed::ClearPointGunAt(ped): out of it, the anim faded
        // CPed::ClearLookFlag(ped) (0x1D4930): bIsLooking (bit 36 of +0x190, GTA III's order) off, bIsRestoringLook on -
        // the head and torso turned back from what he looked at
        constexpr uint32_t kClearLookFlag = 0x001D4930u;
        constexpr uint64_t kFlagLooking = 0x1000000000ull;
        // CPedIK at ped +0x310: the torso's yaw / pitch it holds (+0x31C / +0x320), stepped toward the aim within its
        // limits by PointGunInDirection (0x270730, from 0x1D4F58), whose result is bCanPointGunAtTarget (bit 41)
        constexpr uint32_t kPedTorsoYaw = 0x31Cu, kPedTorsoPitch = 0x320u;
        constexpr uint64_t kFlagCanPointGun = 0x20000000000ull;
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
        std::atomic<bool> g_stickMoved{false}; // the left stick (his walk) pushed, as of the last pad read
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

        // On foot: the in-vehicle flag alone, as FindPlayerVehicle (0x107278) reads it. +0x534 is m_pMyVehicle, kept
        // after he gets out (his last vehicle): with it in the test the aim stayed off after getting out of a car with
        // F (the user's report, 2026-11-16).
        bool onFoot(uint32_t ped) { return !(rd32(ped + kPedDriving) & 0xFFu); }

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

        // A gun he aims with his arm alone, so he can run while aiming and firing: the weapon info's flag 0x80 (+0x6C;
        // VC's CANAIM_WITHARM) - the pistol, the Tec-9 and the MAC-10. In the original these are the ones he runs
        // and fires with; with every other gun firing plants him (checked weapon by weapon, docs/LCS.md).
        bool aimsWithArm(uint32_t ped)
        {
            const int8_t slot = static_cast<int8_t>(g_rdram[(ped + kPedSlot) & kRamMask]);
            if (slot < 0 || slot >= 10)
                return false;
            const uint32_t type = rd32(ped + kPedWeapons + static_cast<uint32_t>(slot) * kSlotSize);
            const uint32_t table = rd32(kWeaponInfoPtr);
            if (type == 0 || type > 36 || !inRam(table) || !inRam(rd32(table + 4)))
                return false;
            return (rd32(rd32(table + 4) + type * kWeaponInfoSize + kWeaponFlags) & kFlagArmAim) != 0;
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

        // Out of the aim as letting go of a target does: CPed::ClearPointGunAt(ped) (FUN_001BAA70) chained - the look
        // and aim flags cleared, AIM_GUN (or ATTACK) back to idle, and the weapon's aim anim (0xC9, or 0xCA) faded out.
        // Writing the state alone left that anim held at speed 0 now and then: Toni frozen in the pose after letting
        // go (the user's report, 2026-11-16). True when it chained the call.
        bool leave(uint32_t ped, const char *why, R5900Context *ctx)
        {
            if (g_state != State::Aiming)
                return false;
            g_state = State::Idle;
            g_aiming.store(false, std::memory_order_relaxed);
            std::fprintf(stderr, "[mouseaim] aim off (%s)\n", why);
            if (!ped)
                return false;
            SET_GPR_U32(ctx, 4, ped);
            ctx->pc = kClearPointGunAt; // returns into the limiter (lcs_overrides.cpp frameBoundary)
            return true;
        }

        // The line of one of the player's shots, down the camera's ray through the circle: from the ray's point
        // nearest the gun, as long as the game's own line. The shotgun's pellets keep their spread about it.
        PS2_GUEST_CC void shotLine(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t ra = GPR_U32(ctx, 31);
            const uint32_t ped = player();
            float f[3];
            // LCS_SHOT_LOG=1: every line test of the player's shots (return address, from, to, the camera, the aim), and
            // the line used while aiming
            static const bool shotLog = std::getenv("LCS_SHOT_LOG") != nullptr;
            if (shotLog && ped && GPR_U32(ctx, 9) == ped && cameraForward(f))
            {
                const uint32_t a = GPR_U32(ctx, 4), b = GPR_U32(ctx, 5);
                std::fprintf(stderr, "[shot] ra %x aim %d from %.2f %.2f %.2f to %.2f %.2f %.2f cam %.3f %.3f %.3f ang %.3f pitch %.3f\n", ra,
                             g_aiming.load() ? 1 : 0, rdf(a), rdf(a + 4), rdf(a + 8), rdf(b), rdf(b + 4), rdf(b + 8), f[0], f[1], f[2],
                             rdf(ped + kPedAimAngle), rdf(ped + kPedPitch));
            }
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
                        // The game fires every pellet level (its end at the gun's height, whatever the pitch: logged)
                        // and spreads them in yaw about his heading. Each keeps its yaw offset from the camera's
                        // heading, turned about the camera's ray: the spread stays across the picture and the pellets
                        // go where the circle is, up and down too (before, only their start was on the ray - level
                        // pellets: the impacts missed the circle, the user's report).
                        const float hx = f[0], hy = f[1], hl = std::sqrt(hx * hx + hy * hy);
                        const float dl = std::sqrt(d[0] * d[0] + d[1] * d[1]);
                        if (hl > 1e-3f && dl > 1e-3f)
                        {
                            const float yaw = std::atan2(hx * d[1] - hy * d[0], hx * d[0] + hy * d[1]); // + to the left
                            const float c = std::cos(yaw), s = std::sin(yaw);
                            const float left[3] = {-hy / hl, hx / hl, 0.0f};
                            for (int i = 0; i < 3; ++i)
                                dir[i] = c * f[i] + s * left[i];
                        }
                        else
                        {
                            for (int i = 0; i < 3; ++i)
                                dir[i] = f[i];
                        }
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
                    if (shotLog)
                        std::fprintf(stderr, "[shot]   -> dir %.3f %.3f %.3f range %.1f\n", dir[0], dir[1], dir[2], range);
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
            out.leftStick = g_aiming.load(std::memory_order_relaxed) && !aimsWithArm(ped);
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
                return leave(ped, !want ? "released" : "no gun / not on foot / another camera", ctx);
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
                    return leave(ped, "the game changed his state", ctx);
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
                if (!aimsWithArm(ped))
                {
                    wrf(ped + kPedRotCur, angle);
                    wrf(ped + kPedRotDest, angle);
                }
                else if (!g_stickMoved.load(std::memory_order_relaxed))
                {
                    // standing: he turns to where the camera looks, quickly but not in one frame (from running
                    // sideways); running, the game turns him to where he runs and the arm aims (the original's way)
                    const float cur = rdf(ped + kPedRotCur);
                    float d = std::remainder(angle - cur, 6.2831853f);
                    const float k = dt > 0.0f ? 1.0f - std::exp(-dt / 0.08f) : 1.0f;
                    const float h = std::fabs(d) < 0.01f ? angle : cur + d * k;
                    wrf(ped + kPedRotCur, h);
                    wrf(ped + kPedRotDest, h);
                }
                wr32(ped + kPedAimTimer, 0u);
                const uint64_t flags = rd64(ped + kPedFlags);
                // Out of a vehicle the torso stays turned (0.87 / 0.37 rad), and from there the IK fails every frame:
                // the gun pointed off to the side until a shot, whose attack straightens the torso (the user's report,
                // 2026-11-17). Straightened here too whenever it fails.
                if (!(flags & kFlagCanPointGun))
                {
                    wrf(ped + kPedTorsoYaw, 0.0f);
                    wrf(ped + kPedTorsoPitch, 0.0f);
                }
                wr64(ped + kPedFlags, (flags & ~kFlagRestoring) | kFlagAiming);
                // He looks at something (getting out of a vehicle he turns his head and torso to it; the game looks
                // about on its own): the look's twist stayed on top of the aim, the gun pointing off to the side until
                // a shot cleared it (the user's report, 2026-11-17). The game's SetPointGunAt only sets a look of its
                // own with a target, and this aim has none: the look is ended as the game ends one.
                if (flags & kFlagLooking)
                {
                    SET_GPR_U32(ctx, 4, ped);
                    ctx->pc = kClearLookFlag;
                    return true;
                }
            }
            return false;
        }
        // not aiming: in when the button is held with a gun, on foot, in the follow camera, standing or walking
        if (want && ped && onFoot(ped) && gunInHand(ped) && followCamera())
        {
            const uint32_t state = rd32(ped + kPedState);
            if ((state == kStateIdle || state == kStateAttack) && (rd64(ped + kPedFlags) & kFlagLooking))
            {
                // a look first ends (see above); into the aim the next frame
                SET_GPR_U32(ctx, 4, ped);
                ctx->pc = kClearLookFlag;
                return true;
            }
            if (state == kStateIdle || state == kStateAttack)
            {
                g_state = State::Aiming;
                g_aiming.store(true, std::memory_order_relaxed);
                wrf(ped + kPedTorsoYaw, 0.0f); // the aim starts from a straight torso (see above)
                wrf(ped + kPedTorsoPitch, 0.0f);
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

    void stick(float lx, float ly) { g_stickMoved.store(std::fabs(lx) + std::fabs(ly) > 0.15f, std::memory_order_relaxed); }

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
