// Mouse aim on Liberty City Stories (docs/LCS.md "Mouse aim"). With Keyboard > Free aim on and the keyboard / mouse the
// device in use, holding the target button (right mouse button) with a gun puts Toni in the game's own aim pose
// without a target - CPed::SetPointGunAt(ped, nullptr) (FUN_001BA618: state AIM_GUN, the weapon's aim anim held),
// chained as a guest call - and keeps him aiming where the camera looks: the gun's direction through SetAimFlag's
// fields (angle +0x6E4, pitch +0xC78, bit 39 of +0x190; AimGun FUN_001D4F58 points the arm), his heading the same. He
// stands while aiming (LCS has no strafing walk for him). The camera moves over his shoulder, a circle marks the middle
// of the picture, and his shots are tested down the camera's line through it. Scoped weapons (the snipers) keep the
// PS2's scope; melee, thrown weapons and the gamepad are untouched. VCS's free aim (game/vcs_aim.h) is the model; LCS
// has no free-aim mode for the pad to enter (docs/LCS.md).
#pragma once

#include "game/vcs_aim.h"

#include <cstdint>

class PS2Runtime;
struct R5900Context;

namespace lcs::mouseaim
{
    // Before runtime.run() (applyLcs): the hook on the shots' line test, the input layout's hooks. LCS_MOUSE_AIM=0:
    // nothing (the PS2's aiming whatever the setting).
    void install(PS2Runtime &runtime);

    // Game thread, at the pad read (vcs::input, GameLayout::aimUpdate): the target button as the player holds it.
    vcs::aim::Output update(const vcs::aim::Input &in);

    // Game thread, at the frame boundary (the limiter hook, before the frame): enters, keeps and leaves the aim. True
    // when it chained a guest call (pc / a0 / a1 set): the hook makes it return into the limiter.
    bool onFrame(uint8_t *rdram, R5900Context *ctx);

    // How far the camera is over the shoulder: 0 not aiming .. 1 aiming (eased). Any thread.
    float cameraShift();

    // Main thread, in the UI pass: the aiming circle while aiming.
    void drawCrosshair();
}
