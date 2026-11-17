// Button prompts by input device (Enhancements > Button prompts; docs/vcs-reference/game-internals.md 6n): the game's prompts - its
// help text's `~k~ ~ACTION~` controls, the font's ✕ ○ △ □ glyphs, the pause menu's help-bar buttons - shown as the
// keyboard keys and mouse buttons the player bound, an Xbox pad's buttons or a PlayStation pad's, following the device
// used last (Automatic) or fixed. Original leaves the game exactly as it is.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class PS2Runtime;

namespace vcs::prompts
{
    // The Enhancements page's Button prompts setting (main.cpp, before the Advanced page).
    void registerSettings();
    // At start-up (applyVcs, after the frontend's hooks: the 2D hooks chain onto theirs): the expander of `~k~`, the
    // font and 2D hooks that find the glyphs drawn, the renderer's glyph classifier, the `prompts` command. Needs the
    // hardware renderer (PS2X_GS=hw); VCS_PROMPTS_HOOKS=0 installs nothing (the setting then does nothing).
    void install(PS2Runtime &runtime);
    // Main thread, each host frame: the glyph atlas made again when the window's size or the bindings change.
    void onHostFrame();
    // Game thread, at the frame kick: the glyph set for the device, the font's glyph table, this frame's glyphs to the
    // renderer.
    void onFrameKick(uint8_t *rdram);

    // ---- shared with Liberty City Stories' prompts (game/lcs_prompts.h) ----------------------------------------------
    // The controls a glyph stands for, the contexts that choose a key for some of them, the glyph sets.
    enum Ctl : int
    {
        Cross, Circle, Triangle, Square, L1, R1, L2, R2, L3, R3, Select, Start,
        DUp, DDown, DLeft, DRight, DPad, DPadUD, DPadLR,
        LS, LSUp, LSDown, LSLeft, LSRight, LSUD, LSLR,
        RS, RSUp, RSDown, RSLeft, RSRight, RSUD, RSLR,
        kCtlCount
    };
    enum Ctx : int
    {
        Foot,
        Vehicle,
        Menu,
        kCtxCount
    };
    enum Set : int
    {
        Original = 0,
        Automatic = 1,
        PlayStation = 2,
        Xbox = 3,
        Keyboard = 4,
    };
    // The set shown now (Enhancements > Button prompts, Automatic resolved by the device used last).
    int shownSet();
    // A control's glyph (game/vcs_glyph_art.h spec) in a set and context.
    std::string glyphSpec(int set, int ctx, int ctl);
    // Every glyph of every set and context (sorted, unique) and the glyphs' height in pixels for the window.
    std::vector<std::string> allGlyphSpecs();
    int glyphCellHeight();
    struct GlyphCell
    {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // 0..1 in the atlas
        float aspect = 1.0f;                  // width / height
    };
    struct GlyphAtlas
    {
        uint32_t w = 0, h = 0;
        std::shared_ptr<const std::vector<uint32_t>> pixels; // alpha in the PS2's 0..128
        std::unordered_map<std::string, GlyphCell> cells;
        std::string signature;
    };
    std::shared_ptr<GlyphAtlas> buildGlyphAtlas(const std::vector<std::string> &specs, int cellH, const std::string &signature);
}
