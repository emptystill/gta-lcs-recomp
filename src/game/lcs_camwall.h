// The on-foot mouse camera kept out of walls (docs/LCS.md "Foot camera and walls"). directCameraResult
// (game/vcs_input.cpp, GameLayout::footCameraClear) asks for the line from the pivot over Toni to where the camera
// would stand; once a frame the frame boundary (lcs_overrides.cpp) chains the game's own line test,
// CWorld::ProcessLineOfSight (FUN_00174538), down that line against buildings, and the answer comes back on the next
// frame: how much of the line is clear. In by a wall at once, out again eased. LCS_FOOT_WALL=0: off.
#pragma once

#include <cstdint>

struct R5900Context;

namespace lcs::camwall
{
    // Game thread, from the camera's result: the camera at pos (the pivot at pivot) - the fraction (0..1] of
    // pos - pivot that is clear, from the last test of a line like it (1 when none).
    float clear(const float pivot[3], const float pos[3]);

    // Game thread, at the frame boundary, once a frame: chains the line test for the last line asked for (pc, a0..,
    // the stack's arguments below a lowered sp). True when it chained.
    bool chain(uint8_t *rdram, R5900Context *ctx);
    // Game thread: the chained test returned (v0 its result; the hook puts sp back after).
    void onReturn(uint8_t *rdram, R5900Context *ctx);
    // How far chain() lowered the stack pointer (the hook raises it again on return).
    constexpr uint32_t kStackSpace = 0x40u;
}
