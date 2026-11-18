// Our settings in Liberty City Stories' own pause menu (the user's wish, 2026-11-17: the F2 menu off, what matters in
// play in the game's menu, the rest in the launcher): rows added to its screen table - Display: Aspect ratio (in the
// place of the PS2's Widescreen row, which the port keeps off), Widescreen HUD and Frame rate cap; Audio: Master volume.
// Their texts come from the game's text lookup (CText::Get, hooked for our keys), left / right / Cross on them change
// the setting instead of reaching the game. docs/LCS.md "Our settings in the pause menu". LCS_MENU_ROWS=0: the
// game's menu as it was.
#pragma once

#include <cstdint>

class PS2Runtime;

namespace lcs::options
{
    void install(PS2Runtime &runtime);
    void onFrameKick(uint8_t *rdram);                     // game thread: the rows in the table (written once, kept)
    void filterPad(uint16_t &buttons, float &lx, float &ly); // game thread, at each pad read (lcs_frontend's delegate)
    void onHostFrame();                                   // main thread: the changes asked for, applied
}
