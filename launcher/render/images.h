// Pictures: decoded once (Windows: WIC, the system's JPEG / PNG decoders; elsewhere none yet), scaled down to the size
// they are needed at, premultiplied and packed into one RGBA8 atlas the renderer samples (render::kImage). Home's
// cover and the logo use them when the player put them in launcher/art (kept out of git: docs/LAUNCHER.md "Home's
// pictures"); without them Home draws its own.
#pragma once

#include "core/math.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace render
{
    struct ImageAtlas
    {
        int size = 0;
        std::vector<uint8_t> rgba; // premultiplied
        uint32_t generation = 0;   // the texture is made again when it changes
        int shelfX = 0, shelfY = 0, shelfH = 0;
    };
    extern ImageAtlas imageAtlas;

    struct Image
    {
        bool ok = false;
        int width = 0, height = 0; // as stored
        float uv[4] = {};          // in the atlas, 0..1
    };

    // decodes `file`, shrinks it so its longer side is at most maxSide, packs it; ok false when it can't
    Image loadImage(const std::filesystem::path &file, int maxSide);

    // `img` filling `dest` the way a cover does (cut to dest's shape around focus, 0..1; zoom >= 1 shows less of it),
    // cut to `mask` (4 corners, or null)
    void drawImageCover(const Image &img, core::Rect dest, const float *mask, float focusX = 0.5f, float focusY = 0.5f,
                        float zoom = 1.0f, float opacity = 1.0f);
    // `img` at its own shape inside `box` (fitted), its top left at the box's top left
    void drawImageFit(const Image &img, core::Rect box, float opacity = 1.0f);
}
