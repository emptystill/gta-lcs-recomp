// Button prompts by input device for Liberty City Stories (Enhancements > Button prompts; docs/LCS.md "Button prompts").
// The game's buttons are pictures: the help text's ✕ ○ △ □ and arrows are cells of the font's last row (font1; the
// expander 0x2F8F58 turns ~X~ into a character of that row), the pause menu's help bar draws the btn_*_PSP textures.
// Both are known in the renderer by their texture (gsHwSetTextureGlyphClassifier) and drawn from the glyph atlas
// VCS's prompts make (game/vcs_prompts.h): the keys and mouse buttons the player bound, or a gamepad's buttons.
#pragma once

#include <cstdint>

class PS2Runtime;

namespace lcs::prompts
{
    // At start-up (the hardware renderer only): the renderer's classifier, the texture finder, the `prompts` command.
    void install(PS2Runtime &runtime);
    // Main thread, each host frame: the glyph atlas made again when the window's size or the bindings change.
    void onHostFrame();
    // Game thread, at the frame kick: the textures found, the context (menu, vehicle, on foot), the glyphs to use.
    void onFrameKick(uint8_t *rdram);
}
