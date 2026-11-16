// Mouse aim on Liberty City Stories (docs/LCS.md "Mouse aim"): the PC aiming of GTA Vice City, still in LCS's code and
// switched off on the PS2. With Keyboard > Free aim on and the keyboard / mouse the device in use, the camera's
// mouse-aim switch (CCamera::m_bUseMouse3rdPerson) is set every frame: firing turns Toni to the camera's direction and
// the shots go down the camera's line through the crosshair's point, he strafes while firing. The game no longer draws
// that crosshair (the PS2 build left it out of CHud::Draw), so it is drawn here, over the picture. With the gamepad, or
// the setting off, the switch stays 0: the PS2's aiming, unchanged.
#pragma once

#include <cstdint>

class PS2Runtime;

namespace lcs::mouseaim
{
    // Before runtime.run() (applyLcs): the hook on the shots' line test. LCS_MOUSE_AIM=0 installs nothing and never
    // sets the switch (the PS2's aiming whatever the setting).
    void install(PS2Runtime &runtime);

    // Game thread, once per game frame (the frame hook, before the frame runs): the switch, and what the crosshair needs.
    void onGameFrame(uint8_t *rdram);

    // Main thread, in the UI pass: the crosshair (Dear ImGui's foreground list), when it is to be shown.
    void drawCrosshair();
}
