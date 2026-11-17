// Liberty City Stories' pause menu with the keyboard and the mouse (docs/LCS.md "The pause menu with the mouse"): Enter
// selects (Cross), Escape goes back (Triangle in a page, Start on the tab bar - the menu closes), a click on a tab goes
// there (again: into it), in a page the wheel moves the selection and the left button selects, the right button goes
// back, on the map the wheel zooms. Everything is the game's own pad: D-pad steps, Cross, Triangle, Start, L1 / R1, as
// if pressed. It stands in for VCS's menu module (vcs::frontend::setDelegate), which game/vcs_input.cpp calls.
#pragma once

#include <cstdint>

class PS2Runtime;

namespace lcs::frontend
{
    // At start-up: the delegate, the `menustate` / `menumouse` commands. LCS_MENU_MOUSE=0: Enter / Escape only.
    void install(PS2Runtime &runtime);
}
