// See vcs_freecam.h and docs/vcs-reference/TRAINER.md.
#include "game/vcs_freecam.h"

#include "game/vcs_aim.h"
#include "game/vcs_input.h"
#include "game/vcs_trainer.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include "runtime/ps2_host_std.h"

namespace vcs::freecam
{
    namespace
    {
        constexpr uint32_t kRamMask = 0x01FFFFFFu;
        // VCS: TheCamera and its CCams (game-internals.md 12b)
        constexpr uint32_t kTheCamera = 0x006F44D0u;
        constexpr Layout kVcsLayout{kTheCamera + 0x50u, kTheCamera + 0x70u, 0x270u, 0x10u, 0x20u, 0u, 0x60u, 0x134u, 0x1A0u};
        constexpr uint32_t kCamHeightRoutines = 0x002A2ED0u; // CCam::ProcessSpecialHeightRoutines(cam), right after
                                                             // CCam::Process (FUN_00294540)

        constexpr float kTurnRate = 2.5f;   // radians a second, the stick at full tilt (the game's own top speed)
        constexpr float kZoomRate = 40.0f;  // degrees a second
        constexpr float kPitchLimit = 1.55f;

        PS2Runtime::RecompiledFunction g_origHeight = nullptr;
        std::atomic<bool> g_installed{false};
        Layout g_layout = kVcsLayout;

        // game thread
        bool g_flying = false;
        float g_x = 0, g_y = 0, g_z = 0, g_yaw = 0, g_pitch = 0, g_fov = 70;
        std::chrono::steady_clock::time_point g_last{};

        std::mutex g_poseMutex;
        Pose g_pose;

        float rdf(const uint8_t *ram, uint32_t a)
        {
            float v;
            std::memcpy(&v, ram + (a & kRamMask), 4);
            return v;
        }
        void wrf(uint8_t *ram, uint32_t a, float v) { std::memcpy(ram + (a & kRamMask), &v, 4); }
        void wrVec(uint8_t *ram, uint32_t a, float x, float y, float z)
        {
            wrf(ram, a, x);
            wrf(ram, a + 4, y);
            wrf(ram, a + 8, z);
        }

        bool activeCam(const uint8_t *ram, uint32_t cam)
        {
            return cam == g_layout.cams + ram[g_layout.activeCam & kRamMask] % 3u * g_layout.camSize;
        }

        // The camera where the game's camera was: no jump when the free camera starts.
        void begin(const uint8_t *ram, uint32_t cam)
        {
            const Layout &L = g_layout;
            g_x = rdf(ram, cam + L.source);
            g_y = rdf(ram, cam + L.source + 4);
            g_z = rdf(ram, cam + L.source + 8);
            const float fx = rdf(ram, cam + L.front), fy = rdf(ram, cam + L.front + 4), fz = rdf(ram, cam + L.front + 8);
            g_yaw = std::atan2(fy, fx);
            g_pitch = std::clamp(std::asin(std::clamp(fz, -1.0f, 1.0f)), -kPitchLimit, kPitchLimit);
            const float fov = rdf(ram, cam + L.fov);
            g_fov = fov >= 10.0f && fov <= 130.0f ? fov : 70.0f;
            g_last = ps2x::hostNow();
            g_flying = true;
            std::fprintf(stderr, "[freecam] on at (%.1f, %.1f, %.1f)\n", g_x, g_y, g_z);
        }

        void end()
        {
            if (!g_flying)
                return;
            g_flying = false;
            std::lock_guard<std::mutex> lock(g_poseMutex);
            g_pose.active = false;
            std::fprintf(stderr, "[freecam] off\n");
        }

        // One camera update: the controls move and turn it (real time, so neither the frame rate nor the game speed
        // changes how fast it flies), then the CCam gets it.
        void update(uint8_t *ram, uint32_t cam)
        {
            const Layout &L = g_layout;
            const auto now = ps2x::hostNow();
            const float dt = std::clamp(std::chrono::duration<float>(now - g_last).count(), 0.0f, 0.1f);
            g_last = now;
            const input::FreeCamInput in = input::takeFreeCameraInput();

            g_yaw = std::remainder(g_yaw + in.turn + in.turnRate * kTurnRate * dt, 6.2831855f);
            g_pitch = std::clamp(g_pitch + in.tilt + in.tiltRate * kTurnRate * dt, -kPitchLimit, kPitchLimit);
            g_fov = std::clamp(g_fov - in.zoom * kZoomRate * dt, 20.0f, 110.0f);

            const float cy = std::cos(g_yaw), sy = std::sin(g_yaw), cp = std::cos(g_pitch), sp = std::sin(g_pitch);
            const float front[3] = {cp * cy, cp * sy, sp};
            const float right[3] = {sy, -cy, 0.0f};
            const float up[3] = {-cy * sp, -sy * sp, cp};
            const float speed = static_cast<float>(trainer::freeCameraSpeed()) * (in.fast ? 4.0f : 1.0f) * (in.slow ? 0.25f : 1.0f);
            const float step = speed * dt;
            g_x += (front[0] * in.forward + right[0] * in.strafe) * step;
            g_y += (front[1] * in.forward + right[1] * in.strafe) * step;
            g_z += (front[2] * in.forward + in.rise) * step;

            wrVec(ram, cam + L.source, g_x, g_y, g_z);
            if (L.sourceCopy)
                wrVec(ram, cam + L.sourceCopy, g_x, g_y, g_z);
            wrVec(ram, cam + L.front, front[0], front[1], front[2]);
            wrVec(ram, cam + L.up, up[0], up[1], up[2]);
            if (L.target)
                wrVec(ram, cam + L.target, g_x + front[0], g_y + front[1], g_z + front[2]);
            wrf(ram, cam + L.fov, g_fov);

            std::lock_guard<std::mutex> lock(g_poseMutex);
            g_pose = {true, g_x, g_y, g_z, g_yaw, g_pitch, g_fov};
        }

        // CCam::ProcessSpecialHeightRoutines(cam a0), which CCamera::Process calls right after the active CCam's update:
        // while flying, the free camera takes the CCam's place here (the game's update has run - a cutscene finishes
        // only when its camera has played its path) and the height routine, which lifts the follow camera over peds
        // near the player, is skipped.
        PS2_GUEST_CC void camHeight(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t cam = GPR_U32(ctx, 4);
            if (!activeCam(rdram, cam))
                return g_origHeight(rdram, ctx, runtime);
            if (!takeCamera(rdram, cam))
            {
                aim::cameraUpdated(cam); // free aim's hand-back to the follow camera (game/vcs_aim.h)
                return g_origHeight(rdram, ctx, runtime);
            }
            ctx->pc = GPR_U32(ctx, 31);
        }
    }

    void install(PS2Runtime &runtime)
    {
        if (const char *e = std::getenv("VCS_FREECAM"); e && e[0] == '0')
            return;
        g_origHeight = runtime.lookupFunction(kCamHeightRoutines);
        if (!g_origHeight)
            return;
        runtime.replaceFunction(kCamHeightRoutines, &camHeight);
        g_installed = true;
    }

    void installLayout(const Layout &layout)
    {
        if (const char *e = std::getenv("VCS_FREECAM"); e && e[0] == '0')
            return;
        g_layout = layout;
        g_installed = true;
    }

    bool takeCamera(uint8_t *rdram, uint32_t cam)
    {
        if (!g_installed.load(std::memory_order_relaxed) || !activeCam(rdram, cam))
            return false;
        if (!flying())
        {
            end();
            return false;
        }
        if (!g_flying)
            begin(rdram, cam);
        update(rdram, cam);
        return true;
    }

    bool flying() { return g_installed.load(std::memory_order_relaxed) && trainer::toggle(trainer::FreeCamera) && trainer::ready(); }

    Pose pose()
    {
        std::lock_guard<std::mutex> lock(g_poseMutex);
        return g_pose;
    }
}
