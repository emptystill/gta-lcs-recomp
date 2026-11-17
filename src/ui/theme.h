// The menus' look (the settings menu F2, the trainer F5): Liberty City Stories' own - the pause menu's colours (its red
// titles, orange tabs, pale blue options on a night-blue dark), a heavy face for titles (DECISIONS.md 2026-11-17: the
// port's menus share nothing with Vice City Stories').
#pragma once

#include "imgui.h"

namespace ui::theme
{
    // the pause menu's colours
    constexpr ImU32 kRed = IM_COL32(178, 18, 26, 255);     // titles ("mapa", "sonido")
    constexpr ImU32 kOrange = IM_COL32(246, 166, 35, 255); // the tab bar ("instrucciones", "juego")
    constexpr ImU32 kBlue = IM_COL32(126, 178, 230, 255);  // options ("VOLUMEN DE LA RADIO")
    constexpr ImU32 kNight = IM_COL32(7, 10, 22, 255);     // the backdrop's dark

    // Pushes the colours and spacing for an ImGui window; pop() after End().
    void push();
    void pop();
    // The titles' face (Impact or the system's heavy sans), or the default font.
    ImFont *titleFont();
}
