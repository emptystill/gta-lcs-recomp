// VCS 2D at the RenderWare level (vcs_hud.cpp).
#pragma once

#include <cstdint>

class PS2Runtime;

namespace vcs
{
    // The game's 2D / HUD routines (game-internals.md 6f; LCS's in lcs_overrides.cpp, docs/LCS.md "HUD"). 0: that
    // part is not hooked. VCS's by default.
    struct HudLayout
    {
        uint32_t im2dRender = 0x002FBE08u;        // (prim, vertices, count): RwIm2DRenderPrimitive
        uint32_t im2dRenderIndexed = 0x002FBE68u; // (prim, vertices, count, indices, indexCount)
        uint32_t hudDraw = 0x0031C628u;           // CHud::Draw
        uint32_t renderStateSet = 0x00175198u;    // RwRenderStateSet(state, value); 6 = rwRENDERSTATEZTESTENABLE
        uint32_t menuDraw = 0x003B53F8u;          // the frontend / pause menu's draw, called from FUN_0021f7d8
        uint32_t loadingScreen = 0x0021E170u;     // GTA's LoadingScreen (splash, text, progress bar, its own frame)
        uint32_t radarDraw = 0x00321500u;         // CHud's radar: map tiles (huge quads, scissored), ring, blips
        uint32_t afterRadar[3] = {0x00113D50u, 0x00321758u, 0x002DFE10u}; // CHud::Draw's next call on each path
        uint32_t fontRender = 0x00261BE8u;        // font render(font, string, x, y, colour, rect, ...): every line
        uint32_t glyphBatch = 0x003DFE24u;        // Im2D's return address in the font's glyph batch (FUN_003dfdd8)
        uint32_t rectFill = 0x003E0B10u;          // the scope mask's side bars (rect, colour, 1) ...
        uint32_t maskLeftReturn = 0x0021F6F0u, maskRightReturn = 0x0021F728u; // ... drawn from these returns
        uint32_t maskLeftReturn2 = 0u, maskRightReturn2 = 0u; // a second pair (LCS: the 2D pass has two scope branches)
        bool maskFromBars = false; // the bars are off screen (LCS: the PSP's top / bottom ones): made full-height strips
        uint32_t spriteUvReturn = 0x003DFFC0u;    // the sprite routine's Im2D return (the tunnel vision's quarters) ...
        uint32_t tunnelVision = 0x00323FE8u, tunnelVisionEnd = 0x0032437Cu; // ... called from in here
    };
    // Before installHudHooks: another game's addresses.
    void setHudLayout(const HudLayout &layout);
    void installHudHooks(PS2Runtime &runtime);
    void onFrameKick(); // the frame's display list goes out: the HUD context ends
    // Widescreen, HUD "Screen edges": each HUD element squeezed towards the screen edge it sits near (any thread)
    void setHudAnchoring(bool on);
    // Widescreen: the HUD's squeeze ((4:3) / aspect; 1 = none, HUD "Stretch"), for the scope mask's side bars (any thread)
    void setHudSqueeze(float factor);
    uint32_t hudDrawCount(); // CHud::Draw calls drawn so far (the trainer's Hide HUD skips them)
}
