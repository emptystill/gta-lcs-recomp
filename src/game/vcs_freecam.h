// The trainer's free camera (docs/vcs-reference/TRAINER.md "Camera"): fly anywhere while the game keeps running and the player stands
// still.
//
// CCamera::Process (FUN_00378dd8) runs the active camera's update, CCam::Process (FUN_00294540), then
// FUN_002a2ed0 (its height routines) on it, and builds the frame's camera - matrix, frustum, culling, streaming - from
// what that CCam left in its source / front / up / FOV (game-internals.md 12b). While the free camera flies the game's
// update still runs (a cutscene ends only when its camera has played its path: replacing the update hung missions on
// "Loading...", 2026-10-30), and the height routines are replaced for the active CCam: the free camera writes those
// fields there and returns. Off, it runs as before; VCS_FREECAM=0 does not hook it at all (the reference).
#pragma once

#include <cstdint>

class PS2Runtime;

namespace vcs::freecam
{
    // Where a game keeps its cameras (a CCam's fields, relative to it)
    struct Layout
    {
        uint32_t activeCam = 0;      // byte: which of the CCams is active
        uint32_t cams = 0, camSize = 0;
        uint32_t front = 0, source = 0, sourceCopy = 0, up = 0, fov = 0, target = 0; // 0: none
    };

    // Before runtime.run(): the hooks (applyVcs).
    void install(PS2Runtime &runtime);
    // [lcs] Liberty City Stories: the camera hook is the game's own (lcs_overrides.cpp, after CCam::Process), which calls
    // takeCamera; this only says where the fields are and turns the free camera on.
    void installLayout(const Layout &layout);
    // Game thread, after the active camera's update: true when the free camera wrote `cam` (the caller then skips the
    // game's own result for it).
    bool takeCamera(uint8_t *rdram, uint32_t cam);

    // Any thread: the trainer's Free camera is on and the game is in play - the pad then gives the game nothing and the
    // keyboard / mouse / gamepad fly the camera (game/vcs_input.h).
    bool flying();

    struct Pose
    {
        bool active = false; // the camera is ours (it has been updated since the free camera was turned on)
        float x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0; // radians: yaw from +x toward +y, pitch up (+)
        float fov = 70;           // degrees
    };
    Pose pose(); // any thread: as of the last camera update
}
