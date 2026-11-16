// See lcs_mouseaim.h and docs/LCS.md "Mouse aim".
#include "game/lcs_mouseaim.h"

#include "game/vcs_input.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ps2_control.h"
#include "render/presenter.h"
#include "settings/settings.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

namespace lcs::mouseaim
{
    namespace
    {
        constexpr uint32_t kRamMask = 0x01FFFFFFu;
        // CCamera::m_bUseMouse3rdPerson (u32). Read by CCam::Using3rdPersonMouseCam (FUN_00286F88: the switch and the
        // follow camera, mode 4) - asked by FireInstantHit (0x2A8DC8: the shot down the camera's line, through
        // Find3rdPersonCamTargetVector 0x28D1B0), the player's weapon code (0x1BABE8: the aim's pitch from the camera,
        // Find3rdPersonQuickAimPitch 0x28D570), the movement (0x1D5830 / 0x1EF700: strafing), the projectile throw
        // (0x2AB418). The PS2 build cleared it every frame (0x34B160, patched out: game/lcs/overrides.toml).
        constexpr uint32_t kUseMouse3rdPerson = 0x003D6C98u;
        constexpr uint32_t kTheCamera = 0x0043BBF0u;
        constexpr uint32_t kActiveCam = 0x10Cu, kCam0 = 0x220u, kCamStride = 0x2B0u, kCamMode = 0x40u;
        constexpr uint32_t kCrosshairX = 0x210u, kCrosshairY = 0x214u; // m_f3rdPersonCHairMultX / Y (0..1 of the screen)
        constexpr uint32_t kModeFollowPed = 4u;
        constexpr uint32_t kPlayerPed = 0x00408E40u; // CWorld::Players[0].m_pPed
        constexpr uint32_t kPedVehicle = 0x534u, kPedDriving = 0x538u;
        constexpr uint32_t kPedSlot = 0x6B8u, kPedWeapons = 0x594u, kWeaponSlotSize = 0x1Cu; // s8 slot; type first
        constexpr uint32_t kWeaponInfoPtr = 0x003D9B48u; // *(*(this) + 4) + type * 0x70; +0 the fire type (0 melee)
        constexpr uint32_t kWeaponInfoSize = 0x70u;
        constexpr uint32_t kFrameCounter = 0x003D9B6Cu; // CTimer::m_FrameCounter: still while the game is paused

        // The weapons' line test (0x2B0BD0; VCS 0x2D0870 by xmatch: from a0, to a1, the shooter t1). LCS's instant-hit
        // shot (its call returns to 0x2A59BC; the pistol, SMGs, rifles...) has no mouse-camera branch any more - only
        // the shotgun's (0x2A8DC8) and the throw (0x2AB418) kept VC's - so its shots followed Toni's heading, not the
        // crosshair (LCS_SHOT_LOG: 6 degrees off once he had turned, more while turning). For the player with mouse aim
        // on, that line is tested down the camera's ray through the crosshair instead, from the point of the ray
        // nearest his gun, as far as the weapon's own line went (VCS's free aim does the same: game/vcs_aim.cpp).
        constexpr uint32_t kShotLine = 0x002B0BD0u;
        constexpr uint32_t kShotLineReturn = 0x002A59BCu;
        constexpr uint32_t kScratch = 0x0007FF00u; // the ray's start: EE kernel RAM no one uses (above the runtime's
                                                   // heap and pools, 0x40000..0x78000)
        PS2Runtime::RecompiledFunction g_origShotLine = nullptr;
        std::atomic<bool> g_on{false};
        std::atomic<uint32_t> g_shots{0};

        struct Shown
        {
            std::atomic<bool> show{false};
            std::atomic<float> x{0.5f}, y{0.5f};
        } g_shown;
        uint32_t g_lastFrame = 0;

        uint32_t rd32(const uint8_t *ram, uint32_t a)
        {
            uint32_t v;
            std::memcpy(&v, ram + (a & kRamMask), 4);
            return v;
        }
        float rdf(const uint8_t *ram, uint32_t a)
        {
            float v;
            std::memcpy(&v, ram + (a & kRamMask), 4);
            return v;
        }
        bool inRam(uint32_t a) { return a >= 0x00100000u && a < 0x02000000u; }

        void wrf(uint8_t *ram, uint32_t a, float v) { std::memcpy(ram + (a & kRamMask), &v, 4); }

        PS2_GUEST_CC void shotLine(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t ped = rd32(rdram, kPlayerPed);
            if (g_on.load(std::memory_order_relaxed) && GPR_U32(ctx, 31) == kShotLineReturn && inRam(ped) && GPR_U32(ctx, 9) == ped)
            {
                // the camera as rendered (TheCamera's matrix: +0x00 right - pointing to the screen's left -, +0x10
                // forward, +0x20 up, +0x30 position) and the view window the crosshair is drawn in: half-height
                // tan(FOV / 2) * 3/4 (the FOV is a 4:3 picture's width), the width that times the picture's aspect
                const uint32_t tc = kTheCamera;
                const uint32_t cam = tc + kCam0 + std::min<uint32_t>(rdram[tc + kActiveCam], 2u) * kCamStride;
                const float fov = rdf(rdram, cam + 0xE8u);
                const float cx = rdf(rdram, tc + kCrosshairX), cy = rdf(rdram, tc + kCrosshairY);
                const uint32_t from = GPR_U32(ctx, 4), to = GPR_U32(ctx, 5);
                float R[3], F[3], U[3], P[3], gun[3], end[3];
                for (int i = 0; i < 3; ++i)
                {
                    R[i] = rdf(rdram, tc + 4u * i);
                    F[i] = rdf(rdram, tc + 0x10u + 4u * i);
                    U[i] = rdf(rdram, tc + 0x20u + 4u * i);
                    P[i] = rdf(rdram, tc + 0x30u + 4u * i);
                    gun[i] = rdf(rdram, from + 4u * i);
                    end[i] = rdf(rdram, to + 4u * i);
                }
                const float halfH = std::tan(fov * 0.5f * 0.017453292f) * 0.75f;
                const float halfW = halfH * present::displayAspect(true);
                const float sx = (cx * 2.0f - 1.0f) * halfW, sy = (1.0f - cy * 2.0f) * halfH;
                float dir[3], len = 0.0f;
                for (int i = 0; i < 3; ++i)
                {
                    dir[i] = F[i] - R[i] * sx + U[i] * sy;
                    len += dir[i] * dir[i];
                }
                len = std::sqrt(len);
                float range = 0.0f;
                for (int i = 0; i < 3; ++i)
                    range += (end[i] - gun[i]) * (end[i] - gun[i]);
                range = std::sqrt(range);
                if (fov > 1.0f && fov < 170.0f && len > 1e-3f && range > 0.01f)
                {
                    for (float &d : dir)
                        d /= len;
                    float along = 0.0f;
                    for (int i = 0; i < 3; ++i)
                        along += (gun[i] - P[i]) * dir[i];
                    along = std::max(0.0f, along);
                    for (int i = 0; i < 3; ++i)
                    {
                        const float start = P[i] + dir[i] * along;
                        wrf(rdram, kScratch + 4u * i, start);
                        wrf(rdram, to + 4u * i, start + dir[i] * range);
                    }
                    wrf(rdram, kScratch + 12u, 0.0f);
                    SET_GPR_U32(ctx, 4, kScratch);
                    g_shots.fetch_add(1, std::memory_order_relaxed);
                }
            }
            g_origShotLine(rdram, ctx, runtime);
        }

        bool enabled()
        {
            static const bool off = [] {
                const char *e = std::getenv("LCS_MOUSE_AIM");
                return e && e[0] == '0';
            }();
            return !off && settings::value("mouse.aim") != 0 && settings::value("mouse.camera") != 0 &&
                   vcs::input::lastDevice() == vcs::input::Device::Keyboard;
        }

        // A gun in his hand: the current slot's weapon fires (its info's fire type is not 0, melee).
        bool gunInHand(const uint8_t *ram, uint32_t ped)
        {
            const int8_t slot = static_cast<int8_t>(ram[(ped + kPedSlot) & kRamMask]);
            if (slot < 0 || slot >= 10)
                return false;
            const uint32_t type = rd32(ram, ped + kPedWeapons + static_cast<uint32_t>(slot) * kWeaponSlotSize);
            const uint32_t table = rd32(ram, kWeaponInfoPtr);
            if (type == 0 || type > 60 || !inRam(table))
                return false;
            const uint32_t infos = rd32(ram, table + 4);
            return inRam(infos) && rd32(ram, infos + type * kWeaponInfoSize) != 0;
        }
    }

    void install(PS2Runtime &runtime)
    {
        if (const char *e = std::getenv("LCS_MOUSE_AIM"); e && e[0] == '0')
        {
            std::fprintf(stderr, "[mouseaim] off (LCS_MOUSE_AIM=0)\n");
            return;
        }
        g_origShotLine = runtime.lookupFunction(kShotLine);
        if (g_origShotLine)
            runtime.replaceFunction(kShotLine, &shotLine);
        // aimstate -> ok active=<0|1> shots=<player shots down the camera's ray so far>
        ps2control::addCommand("aimstate", [](const std::string &, ps2control::Reply reply) {
            char text[96];
            std::snprintf(text, sizeof(text), "ok active=%d shots=%u", g_on.load() ? 1 : 0, g_shots.load());
            reply(text);
        });
    }

    void onGameFrame(uint8_t *rdram)
    {
        if (!rdram)
            return;
        const bool on = enabled();
        g_on.store(on, std::memory_order_relaxed);
        const uint32_t want = on ? 1u : 0u;
        if (rd32(rdram, kUseMouse3rdPerson) != want)
            std::memcpy(rdram + kUseMouse3rdPerson, &want, 4);

        // the crosshair: on foot, the follow camera, a gun in his hand, the game running
        bool show = false;
        const uint32_t frame = rd32(rdram, kFrameCounter);
        const bool running = frame != g_lastFrame;
        g_lastFrame = frame;
        const uint32_t ped = rd32(rdram, kPlayerPed);
        if (on && running && inRam(ped) && !(rd32(rdram, ped + kPedDriving) & 0xFFu) && !inRam(rd32(rdram, ped + kPedVehicle)))
        {
            const uint32_t active = rdram[kTheCamera + kActiveCam];
            const uint32_t cam = kTheCamera + kCam0 + std::min(active, 2u) * kCamStride;
            if ((rd32(rdram, cam + kCamMode) & 0xFFFFu) == kModeFollowPed && gunInHand(rdram, ped))
            {
                const float x = rdf(rdram, kTheCamera + kCrosshairX), y = rdf(rdram, kTheCamera + kCrosshairY);
                if (x > 0.0f && x < 1.0f && y > 0.0f && y < 1.0f)
                {
                    g_shown.x.store(x, std::memory_order_relaxed);
                    g_shown.y.store(y, std::memory_order_relaxed);
                    show = true;
                }
            }
        }
        g_shown.show.store(show, std::memory_order_relaxed);
    }

    void drawCrosshair()
    {
        if (!g_shown.show.load(std::memory_order_relaxed) || !ImGui::GetCurrentContext())
            return;
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        float x0 = 0, y0 = 0, w = 0, h = 0;
        present::fitRect(present::displayAspect(true), 0.0f, vp->Size.x, vp->Size.y, x0, y0, w, h);
        const float cx = vp->Pos.x + x0 + g_shown.x.load(std::memory_order_relaxed) * w;
        const float cy = vp->Pos.y + y0 + g_shown.y.load(std::memory_order_relaxed) * h;
        // a small cross with a gap and a dot, white over a dark outline, sized by the picture's height
        const float s = std::max(1.0f, h / 1080.0f);
        const float gap = 5.0f * s, len = 9.0f * s, thick = 2.0f * s;
        ImDrawList *draw = ImGui::GetForegroundDrawList();
        const ImU32 dark = IM_COL32(0, 0, 0, 170), light = IM_COL32(255, 255, 255, 230);
        auto arm = [&](float dx, float dy, ImU32 col, float t, float extra) {
            draw->AddLine(ImVec2(cx + dx * (gap - extra), cy + dy * (gap - extra)),
                          ImVec2(cx + dx * (gap + len + extra), cy + dy * (gap + len + extra)), col, t);
        };
        for (int pass = 0; pass < 2; ++pass)
        {
            const ImU32 col = pass == 0 ? dark : light;
            const float t = pass == 0 ? thick + 2.0f * s : thick, extra = pass == 0 ? s : 0.0f;
            arm(1, 0, col, t, extra);
            arm(-1, 0, col, t, extra);
            arm(0, 1, col, t, extra);
            arm(0, -1, col, t, extra);
            draw->AddCircleFilled(ImVec2(cx, cy), pass == 0 ? 2.2f * s : 1.2f * s, col);
        }
    }
}
