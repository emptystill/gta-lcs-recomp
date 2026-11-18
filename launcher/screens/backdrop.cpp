// Behind everything: a night-blue sky and, on Home, a comic book's cover in Liberty City Stories' manner - three slanted
// panels with thick black borders on a halftone ground, and a caption box. In the panels: the player's pictures when
// launcher/art has them (cover-1..3: the user's choice, 2026-11-17; render/images.h), each
// cut to its panel and drifting slowly closer; else the launcher's own drawings - a made man in a fedora with a
// cigarette (red), Liberty City's skyline and its suspension bridge at dusk (orange), a 1970s sedan at speed (blue).
// Alive while Home shows and the game isn't running: the pictures drift, the smoke curls up, windows go on and off,
// searchlights sweep, the speed lines run, the halftone breathes - at 30 frames a second (15 when the window isn't
// focused); standing still with reduce motion on. Every other screen draws the sky once and sleeps. (DECISIONS.md
// 2026-11-17: the launcher's look is LCS's.)
#include "shell.h"

#include "platform/platform.h"
#include "render/images.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdint>

namespace screens
{
    using namespace render;
    using core::mix;
    using core::withAlpha;

    namespace
    {
        // the same "random" numbers every frame: a hash of an index into [0, 1)
        float hash(uint32_t n)
        {
            n ^= n >> 16;
            n *= 0x7FEB352Du;
            n ^= n >> 15;
            n *= 0x846CA68Bu;
            n ^= n >> 16;
            return float(n & 0xFFFFFF) / 16777216.0f;
        }

        struct Scene
        {
            float w = 0, h = 0, s = 0; // size, and the smaller side (what the scene scales with)
            float t = 0;               // the ambient clock, seconds
        };

        // the comic's own colours (both palettes: the panels are printed, they don't follow the theme)
        constexpr Color kInk = core::rgb(0x07080D);
        constexpr Color kPanelRed = core::rgb(0xB8141E);
        constexpr Color kPanelRedDark = core::rgb(0x7A0C13);
        constexpr Color kPanelOrange = core::rgb(0xF4A12A);
        constexpr Color kPanelOrangeHi = core::rgb(0xFFD27A);
        constexpr Color kPanelBlue = core::rgb(0x2D5C9E);
        constexpr Color kPanelBlueHi = core::rgb(0x7EB2E6);
        constexpr Color kPaper = core::rgb(0xFFF3C4);
        constexpr Color kWhite = core::rgb(0xF4F1EA);

        struct Pt
        {
            float x, y;
        };
        struct Panel
        {
            Pt p[4]; // clockwise from the top left, in the scene's fractions
        };
        // the three panels, in fractions of the window; gutters between them
        constexpr Panel kPanels[] = {
            {{{0.505f, 0.135f}, {0.705f, 0.115f}, {0.668f, 0.800f}, {0.505f, 0.800f}}}, // the made man
            {{{0.725f, 0.112f}, {0.975f, 0.135f}, {0.975f, 0.445f}, {0.708f, 0.475f}}}, // the skyline
            {{{0.704f, 0.505f}, {0.975f, 0.475f}, {0.975f, 0.800f}, {0.686f, 0.800f}}}, // the sedan
        };

        Pt at(const Scene &S, Pt f) { return {f.x * S.w, f.y * S.h}; }

        void fillQuad(Pt a, Pt b, Pt c, Pt d, Color col)
        {
            const float q[8] = {a.x, a.y, b.x, b.y, c.x, c.y, d.x, d.y};
            quad(q, col);
        }

        // a panel: its drop shadow, the black border (the quad grown about its centre), the flat fill; its corners in
        // pixels to `corners` (8 floats)
        void panel(const Scene &S, const Panel &P, Color fill, Rect &box, float *corners)
        {
            Pt p[4];
            float cx = 0, cy = 0;
            for (int i = 0; i < 4; ++i)
            {
                p[i] = at(S, P.p[i]);
                cx += p[i].x * 0.25f;
                cy += p[i].y * 0.25f;
            }
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (const Pt &q : p)
            {
                x0 = std::min(x0, q.x);
                y0 = std::min(y0, q.y);
                x1 = std::max(x1, q.x);
                y1 = std::max(y1, q.y);
            }
            shadow({x0 + px(6), y0 + px(10), x1 - x0, y1 - y0}, withAlpha(kInk, 0.55f), px(14), px(4));
            // what is drawn in it stays in the largest upright rectangle inside it (the renderer clips to rectangles):
            // the slant leaves a strip of flat colour along an edge, as a printed panel's margin
            const float ix0 = std::max(p[0].x, p[3].x), ix1 = std::min(p[1].x, p[2].x);
            const float iy0 = std::max(p[0].y, p[1].y), iy1 = std::min(p[2].y, p[3].y);
            box = {ix0, iy0, ix1 - ix0, iy1 - iy0};
            const float b = px(4.5f);
            auto grow = [&](Pt q) {
                const float dx = q.x - cx, dy = q.y - cy, len = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
                return Pt{q.x + dx / len * b * 1.4f, q.y + dy / len * b * 1.4f};
            };
            fillQuad(grow(p[0]), grow(p[1]), grow(p[2]), grow(p[3]), kInk);
            fillQuad(p[0], p[1], p[2], p[3], fill);
            for (int i = 0; i < 4; ++i)
            {
                corners[i * 2] = p[i].x;
                corners[i * 2 + 1] = p[i].y;
            }
        }

        // the pictures (launcher/art), loaded once: the three panels' and the logo
        struct Art
        {
            bool tried = false;
            render::Image image[5]; // the cover's three, the logo, the pause menu's backdrop
        };
        Art g_art;

        // halftone: dots on a slanted grid, larger toward `light` (a comic's printed shading); `breathe` swells them
        void halftone(Rect area, float cell, Color col, Pt light, float reach, float breathe, float maxR)
        {
            pushClip(area);
            for (float y = area.y - cell; y < area.y + area.h + cell; y += cell)
            {
                const int row = int((y - area.y) / cell);
                for (float x = area.x - cell + (row & 1) * cell * 0.5f; x < area.x + area.w + cell; x += cell)
                {
                    const float d = std::sqrt((x - light.x) * (x - light.x) + (y - light.y) * (y - light.y)) / reach;
                    const float r = maxR * std::clamp(1.0f - d, 0.0f, 1.0f) * (0.85f + 0.15f * breathe);
                    if (r > px(0.6f))
                        circle(x, y, r, col);
                }
            }
            popClip();
        }

        // ---- panel 1: a made man - fedora, broad shoulders, a cigarette and its smoke ---------------------------------
        void madeMan(const Scene &S, Rect box)
        {
            pushClip({box.x + px(2), box.y + px(2), box.w - px(4), box.h - px(4)});
            halftone(box, px(9), withAlpha(kPanelRedDark, 0.9f), {box.x + box.w * 0.15f, box.y + box.h * 0.95f}, box.h * 0.9f,
                     0.5f + 0.5f * std::sin(S.t * 0.6f), px(4));
            const float W = box.w, H = box.h, cx = box.x + W * 0.47f, top = box.y;
            const Color shape = kInk;
            // sized by the panel's width (it is tall and narrow): a head a third of it, shoulders filling it
            const float headR = W * 0.20f, hx = cx + W * 0.02f, hy = top + H * 0.40f;
            const float shoulderY = hy + headR * 1.55f, bottom = box.y + box.h + px(4);
            // shoulders and coat, the lapels' white edges, the shirt and tie
            fillQuad({cx - W * 0.62f, bottom}, {cx - W * 0.34f, shoulderY}, {cx + W * 0.36f, shoulderY}, {cx + W * 0.66f, bottom}, shape);
            fillQuad({cx - W * 0.09f, shoulderY}, {cx + W * 0.09f, shoulderY}, {cx + W * 0.04f, shoulderY + H * 0.22f},
                     {cx - W * 0.04f, shoulderY + H * 0.22f}, kWhite);
            fillQuad({cx - W * 0.03f, shoulderY + H * 0.01f}, {cx + W * 0.03f, shoulderY + H * 0.01f}, {cx + W * 0.045f, shoulderY + H * 0.20f},
                     {cx - W * 0.045f, shoulderY + H * 0.20f}, kPanelRedDark);
            line(cx - W * 0.09f, shoulderY, cx - W * 0.20f, shoulderY + H * 0.30f, px(3), kWhite);
            line(cx + W * 0.09f, shoulderY, cx + W * 0.20f, shoulderY + H * 0.30f, px(3), kWhite);
            // neck and head (turned a little to the right: the cigarette out on that side)
            rect({hx - headR * 0.45f, hy, headR * 0.9f, shoulderY - hy + px(2)}, shape);
            circle(hx, hy, headR, shape);
            // the jaw's lit edge
            arc(hx, hy, headR - px(2), px(2.5f), 0.15f, 1.25f, withAlpha(kPanelOrangeHi, 0.75f));
            // the fedora: a crown with its dent and band, and the brim
            const float brimY = hy - headR * 0.45f;
            fillQuad({hx - headR * 0.90f, brimY}, {hx - headR * 0.72f, brimY - headR * 0.95f}, {hx + headR * 0.78f, brimY - headR * 1.0f},
                     {hx + headR * 0.95f, brimY}, shape);
            rect({hx - headR * 0.91f, brimY - headR * 0.32f, headR * 1.86f, headR * 0.22f}, kPanelRedDark);
            rect({hx - headR * 1.55f, brimY - px(2), headR * 3.1f, headR * 0.22f}, shape, Corners(px(4)));
            line(hx - headR * 0.1f, brimY - headR * 0.95f, hx + headR * 0.05f, brimY - headR * 0.70f, px(2), kPanelRedDark);
            // the cigarette and its ember
            const float mx = hx + headR * 0.75f, my = hy + headR * 0.45f;
            line(mx, my, mx + headR * 0.85f, my + headR * 0.12f, px(3), kWhite);
            const float glow = 0.75f + 0.25f * std::sin(S.t * 2.3f);
            circle(mx + headR * 0.88f, my + headR * 0.12f, px(3.2f), mix(kPanelOrange, kPanelOrangeHi, glow));
            // the smoke: puffs that rise, drift and fade, one after another
            for (int i = 0; i < 9; ++i)
            {
                const float phase = std::fmod(S.t * 0.22f + i / 9.0f, 1.0f);
                const float sx = mx + headR * 0.9f + std::sin(phase * 7.0f + i) * headR * 0.35f + phase * headR * 0.6f;
                const float sy = my - phase * H * 0.42f;
                const float r = px(3) + phase * headR * 0.55f;
                circle(sx, sy, r, withAlpha(kWhite, 0.30f * (1.0f - phase) * std::min(1.0f, phase * 6.0f)));
            }
            popClip();
        }

        // ---- panel 2: Liberty City at dusk - towers, lit windows, the suspension bridge, searchlights --------------------
        void skyline(const Scene &S, Rect box)
        {
            pushClip({box.x + px(2), box.y + px(2), box.w - px(4), box.h - px(4)});
            const float W = box.w, H = box.h, ground = box.y + H * 0.80f;
            // the low sun behind the city
            circle(box.x + W * 0.30f, ground - H * 0.05f, H * 0.36f, withAlpha(kPanelOrangeHi, 0.85f));
            halftone({box.x, box.y, W, H}, px(8), withAlpha(kPanelRedDark, 0.55f), {box.x + W, box.y}, H * 1.2f,
                     0.5f + 0.5f * std::sin(S.t * 0.5f + 1.0f), px(3.2f));
            // searchlights from behind the towers
            for (int i = 0; i < 2; ++i)
            {
                const float bx = box.x + W * (0.38f + 0.34f * i), a = -1.57f + 0.45f * std::sin(S.t * (0.35f + 0.12f * i) + i * 2.1f);
                const float len = H * 1.1f, spread = 0.05f;
                fillQuad({bx - px(2), ground}, {bx + std::cos(a - spread) * len, ground + std::sin(a - spread) * len},
                         {bx + std::cos(a + spread) * len, ground + std::sin(a + spread) * len}, {bx + px(2), ground},
                         withAlpha(kPaper, 0.22f));
            }
            // the bridge: two towers, the main cables, hangers, the deck - off to the right, over the water
            const float t0 = box.x + W * 0.70f, t1 = box.x + W * 0.93f, towerTop = ground - H * 0.46f, deck = ground - H * 0.12f;
            for (float tx : {t0, t1})
            {
                rect({tx - px(3), towerTop, px(6), ground - towerTop}, kInk);
                rect({tx - px(6), deck - px(10), px(12), px(4)}, kInk);
            }
            auto cable = [&](float xa, float ya, float xb, float yb, float sag) {
                Pt prev{xa, ya};
                for (int k = 1; k <= 12; ++k)
                {
                    const float u = k / 12.0f;
                    const Pt p{xa + (xb - xa) * u, ya + (yb - ya) * u + sag * 4.0f * u * (1.0f - u)};
                    line(prev.x, prev.y, p.x, p.y, px(2), kInk);
                    if (k < 12 && k % 2 == 0)
                        line(p.x, p.y, p.x, deck, px(1), withAlpha(kInk, 0.8f));
                    prev = p;
                }
            };
            cable(box.x + W * 0.52f, deck - px(2), t0, towerTop, -H * 0.02f);
            cable(t0, towerTop, t1, towerTop, H * 0.27f);
            cable(t1, towerTop, box.x + W * 1.05f, deck, -H * 0.02f);
            rect({box.x + W * 0.50f, deck, W * 0.55f, px(5)}, kInk);
            // the towers of Portland and Staunton: setbacks and water tanks, lit windows going on and off
            struct Tower
            {
                float x, w, h;
                int style;
            };
            constexpr Tower kTowers[] = {{0.02f, 0.09f, 0.34f, 1}, {0.10f, 0.07f, 0.52f, 2}, {0.17f, 0.10f, 0.40f, 0},
                                         {0.27f, 0.06f, 0.62f, 3}, {0.33f, 0.09f, 0.45f, 1}, {0.42f, 0.07f, 0.30f, 2},
                                         {0.49f, 0.08f, 0.38f, 0}, {0.57f, 0.05f, 0.24f, 0}};
            uint32_t n = 0;
            for (const Tower &T : kTowers)
            {
                const float bx = box.x + W * T.x, bw = W * T.w, bh = H * T.h, by = ground - bh;
                rect({bx, by, bw, bh + px(2)}, kInk);
                if (T.style == 1)
                    rect({bx + bw * 0.2f, by - bh * 0.08f, bw * 0.6f, bh * 0.08f + px(1)}, kInk);
                if (T.style == 2) // a water tank on legs
                {
                    rect({bx + bw * 0.25f, by - bh * 0.10f, bw * 0.5f, bh * 0.07f}, kInk, Corners(px(2), px(2), 0, 0));
                    line(bx + bw * 0.3f, by - bh * 0.03f, bx + bw * 0.3f, by, px(1.5f), kInk);
                    line(bx + bw * 0.7f, by - bh * 0.03f, bx + bw * 0.7f, by, px(1.5f), kInk);
                }
                if (T.style == 3) // a crown and a mast
                {
                    fillQuad({bx, by}, {bx + bw * 0.5f, by - bh * 0.12f}, {bx + bw * 0.5f, by - bh * 0.12f}, {bx + bw, by}, kInk);
                    line(bx + bw * 0.5f, by - bh * 0.12f, bx + bw * 0.5f, by - bh * 0.25f, px(1.5f), kInk);
                }
                // windows: a grid, each lit or not, a few changing now and then
                const float ww = std::max(px(2), bw * 0.12f), wh = std::max(px(2), H * 0.018f);
                for (float wy = by + wh * 2; wy < ground - wh * 2; wy += wh * 2.4f)
                    for (float wx = bx + ww; wx < bx + bw - ww; wx += ww * 2.0f)
                    {
                        ++n;
                        const float slot = std::floor(S.t / (6.0f + 9.0f * hash(n * 7)));
                        if (hash(n * 31u + uint32_t(slot) * 977u) < 0.45f)
                            rect({wx, wy, ww, wh}, kPanelOrangeHi);
                    }
            }
            // the water below, and the city's reflection as broken strokes
            rect({box.x, ground, W, box.y + H - ground + px(4)}, kInk);
            for (int i = 0; i < 9; ++i)
            {
                const float y = ground + (i + 1) * (box.y + H - ground) / 10.0f;
                const float shift = std::fmod(S.t * 12.0f * (1 + i % 3) + hash(uint32_t(i) * 13) * W, W);
                for (int k = 0; k < 3; ++k)
                {
                    const float x = box.x + std::fmod(shift + k * W * 0.37f, W);
                    line(x, y, x + W * (0.05f + 0.06f * hash(uint32_t(i * 3 + k))), y, px(1.5f), withAlpha(kPanelOrange, 0.55f));
                }
            }
            popClip();
        }

        // ---- panel 3: a 1970s sedan at speed - long hood, the roof's pillar, chrome, speed lines ---------------------------
        void sedan(const Scene &S, Rect box)
        {
            pushClip({box.x + px(2), box.y + px(2), box.w - px(4), box.h - px(4)});
            const float W = box.w, H = box.h;
            halftone(box, px(9), withAlpha(kPanelBlueHi, 0.35f), {box.x + W * 0.9f, box.y + H * 0.1f}, H * 1.1f,
                     0.5f + 0.5f * std::sin(S.t * 0.7f + 2.0f), px(4));
            // speed lines behind it
            for (int i = 0; i < 14; ++i)
            {
                const float y = box.y + H * (0.18f + 0.62f * hash(uint32_t(i) * 17));
                const float len = W * (0.18f + 0.25f * hash(uint32_t(i) * 23));
                const float x = box.x + W - std::fmod(S.t * W * (0.9f + 0.8f * hash(uint32_t(i) * 29)) + hash(uint32_t(i) * 31) * W * 2.0f, W * 1.6f);
                line(x, y, x + len, y, px(2), withAlpha(kWhite, 0.55f));
            }
            // the car, facing left, bobbing a little on its springs
            const float bob = std::sin(S.t * 9.0f) * px(1.2f);
            const float cx = box.x + W * 0.48f, base = box.y + H * 0.70f + bob, L = W * 0.78f, bodyH = H * 0.16f;
            const float x0 = cx - L * 0.5f;
            // the greenhouse: windscreen and rear window slanted, the pillar between the windows
            const float roofY = base - bodyH - H * 0.13f;
            fillQuad({x0 + L * 0.28f, base - bodyH}, {x0 + L * 0.38f, roofY}, {x0 + L * 0.68f, roofY}, {x0 + L * 0.78f, base - bodyH}, kInk);
            fillQuad({x0 + L * 0.31f, base - bodyH - px(2)}, {x0 + L * 0.395f, roofY + px(4)}, {x0 + L * 0.52f, roofY + px(4)},
                     {x0 + L * 0.52f, base - bodyH - px(2)}, withAlpha(kPanelBlueHi, 0.85f));
            fillQuad({x0 + L * 0.545f, base - bodyH - px(2)}, {x0 + L * 0.545f, roofY + px(4)}, {x0 + L * 0.665f, roofY + px(4)},
                     {x0 + L * 0.75f, base - bodyH - px(2)}, withAlpha(kPanelBlueHi, 0.85f));
            // the body: long and low, a chrome line, the bumpers
            rect({x0, base - bodyH, L, bodyH}, kInk, Corners(px(8), px(10), px(4), px(4)));
            line(x0 + px(6), base - bodyH * 0.55f, x0 + L - px(6), base - bodyH * 0.55f, px(2), withAlpha(kWhite, 0.8f));
            rect({x0 - px(4), base - bodyH * 0.38f, px(8), bodyH * 0.3f}, kWhite, Corners(px(2)));
            rect({x0 + L - px(4), base - bodyH * 0.38f, px(8), bodyH * 0.3f}, kWhite, Corners(px(2)));
            // the headlight, on: a beam ahead of it
            circle(x0 + px(5), base - bodyH * 0.7f, px(3.5f), kPaper);
            fillQuad({x0 + px(4), base - bodyH * 0.78f}, {x0 - W * 0.30f, base - bodyH * 1.5f}, {x0 - W * 0.30f, base + bodyH * 0.2f},
                     {x0 + px(4), base - bodyH * 0.62f}, withAlpha(kPaper, 0.25f));
            // the wheels, turning: tyres, hubs and a spoke that goes round
            for (float wx : {x0 + L * 0.20f, x0 + L * 0.80f})
            {
                const float r = H * 0.085f, wy = base + px(1) - bob;
                circle(wx, wy, r, kInk);
                circle(wx, wy, r * 0.55f, core::rgb(0x9AA3B5));
                const float a = -S.t * 14.0f;
                line(wx + std::cos(a) * r * 0.5f, wy + std::sin(a) * r * 0.5f, wx - std::cos(a) * r * 0.5f, wy - std::sin(a) * r * 0.5f,
                     px(2), kInk);
            }
            // the road
            rect({box.x, base + H * 0.085f, W, px(4)}, kInk);
            popClip();
        }

        // the comic's caption box: yellowed paper, a black border, the heavy face
        void caption(Rect box, const char *text, bool atBottom)
        {
            const float size = px(15);
            const float tw = measure(kFontBrand, size, text);
            const float h = size * 1.45f;
            // at the top over the drawing; at the bottom over a picture (its face is up there)
            const Rect r{box.x + px(10), atBottom ? box.bottom() - h - px(12) : box.y + px(10), tw + px(16), h};
            rect({r.x - px(2.5f), r.y - px(2.5f), r.w + px(5), r.h + px(5)}, kInk);
            rect(r, kPaper);
            drawText(kFontBrand, size, r.x + px(8), r.y + size * 1.08f, kInk, text);
        }

        void sky(const Scene &S, float hero)
        {
            gradient({0, 0, S.w, S.h}, theme::kSky, theme::kNight);
            // the game's pause menu's backdrop (dark: its navy silhouettes of a fire escape and a bike; from the player's
            // installed frontend pack), the whole window, drifting slowly; the light theme keeps its paper
            if (const render::Image *menu = homeArt(4); menu && darkTheme(*shell.app))
            {
                const float drift = 0.5f - 0.5f * std::cos(S.t * 2.0f * core::kPi / 60.0f);
                render::drawImageCover(*menu, {0, 0, S.w, S.h}, nullptr, 0.45f + 0.1f * drift, 0.45f, 1.06f + 0.04f * drift, 0.92f);
            }
            // a warm glow behind the cover, Home only; a cool one low on the left
            shadow({S.w * 0.55f, S.h * 0.05f, S.w * 0.5f, S.h * 0.7f}, withAlpha(theme::kPink, 0.05f + 0.07f * hero), S.s * 0.22f, S.s * 0.2f);
            shadow({-S.w * 0.2f, S.h * 0.65f, S.w * 0.55f, S.h * 0.5f}, withAlpha(theme::kHorizon, 0.35f), S.s * 0.25f, S.s * 0.2f);
        }
    }

    const render::Image *homeArt(int which)
    {
        if (!g_art.tried && shell.app)
        {
            g_art.tried = true;
            const std::filesystem::path dir = shell.app->root / "launcher" / "art";
            // the longest side each is stored at: as large as Home shows it on a 4K screen, and all four fit the atlas
            static const char *kNames[4] = {"cover-1", "cover-2", "cover-3", "logo"};
            static const int kSides[4] = {1200, 700, 900, 600};
            for (int i = 0; i < 4; ++i)
                for (const char *ext : {".jpg", ".png", ".jpeg"})
                {
                    const std::filesystem::path file = dir / (std::string(kNames[i]) + ext);
                    std::error_code ec;
                    if (std::filesystem::is_regular_file(file, ec))
                    {
                        g_art.image[i] = render::loadImage(file, kSides[i]);
                        break;
                    }
                }
            // the pause menu's backdrop: from the frontend pack tools/hud_pack.py installed (<data>/hud_pack: raw RGBA,
            // index.txt names each file's source) - the game's own texture, so read from the player's pack, never
            // shipped; its PCSX2 file name is the pack's ("Remastered Frontend PCSX2 [LCS]")
            std::ifstream index(shell.app->savesFolder() / "hud_pack" / "index.txt");
            std::string line;
            while (std::getline(index, line))
                if (line.find("69836e67aa9f75b6-4c033bf7e4e65774-00006214") != std::string::npos)
                {
                    std::istringstream in(line);
                    int pw = 0, ph = 0, w = 0, h = 0;
                    std::string probe, name;
                    if (in >> pw >> ph >> w >> h >> probe >> name)
                        g_art.image[4] = render::loadRawImage(shell.app->savesFolder() / "hud_pack" / name, w, h, 1024, true, 1.8f);
                    break;
                }
        }
        return which >= 0 && which < 5 && g_art.image[which].ok ? &g_art.image[which] : nullptr;
    }

    void drawBackdrop(Rect r, float hero, bool alive)
    {
        Scene S;
        S.w = r.w;
        S.h = r.h;
        S.s = std::min(r.w, r.h);
        // the ambient clock: real time (a still frame with reduce motion on)
        S.t = ui::state.motion > 0 ? float(std::fmod(ui::state.wall + shell.options.clock, 3600.0)) : 20.0f;

        sky(S, hero);
        if (hero <= 0.01f)
            return;
        const float e = core::easeOutCubic(hero);
        pushOpacity(hero);
        pushOffset(0, (1.0f - e) * px(24)); // the cover rises a little as Home appears
        // the halftone ground behind the panels
        halftone({S.w * 0.47f, S.h * 0.06f, S.w * 0.53f, S.h * 0.80f}, px(14), withAlpha(theme::kHorizon, 0.55f),
                 {S.w * 0.78f, S.h * 0.40f}, S.s * 0.75f, 0.5f + 0.5f * std::sin(S.t * 0.4f), px(5.5f));
        Rect boxes[3];
        float corners[3][8];
        const Color fills[3] = {kPanelRed, kPanelOrange, kPanelBlue};
        for (int i = 0; i < 3; ++i)
            panel(S, kPanels[i], fills[i], boxes[i], corners[i]);
        // the pictures: each where its face is (focus), drifting closer and back over half a minute or so
        struct Look
        {
            float fx, fy, period;
        };
        constexpr Look kLooks[3] = {{0.57f, 0.36f, 31.0f}, {0.46f, 0.45f, 27.0f}, {0.50f, 0.58f, 35.0f}}; // the taxi and the van: their fronts
        for (int i = 0; i < 3; ++i)
        {
            const render::Image *img = homeArt(i);
            if (!img)
                continue;
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (int k = 0; k < 4; ++k)
            {
                x0 = std::min(x0, corners[i][k * 2]);
                x1 = std::max(x1, corners[i][k * 2]);
                y0 = std::min(y0, corners[i][k * 2 + 1]);
                y1 = std::max(y1, corners[i][k * 2 + 1]);
            }
            const float drift = 0.5f - 0.5f * std::cos(S.t * 2.0f * core::kPi / kLooks[i].period + float(i) * 1.7f);
            render::drawImageCover(*img, {x0, y0, x1 - x0, y1 - y0}, corners[i], kLooks[i].fx, kLooks[i].fy, 1.02f + 0.07f * drift);
        }
        if (!homeArt(0))
            madeMan(S, boxes[0]);
        if (!homeArt(1))
            skyline(S, boxes[1]);
        if (!homeArt(2))
            sedan(S, boxes[2]);
        caption(boxes[0], "LIBERTY CITY, 1998", homeArt(0) != nullptr);
        popOffset();
        popOpacity();

        // the next frame of the scene; never while the game runs (it wants the GPU) or with reduce motion on
        if (alive && ui::state.motion > 0)
            ui::wakeAt(ui::state.wall + (platform::window.focused ? 1.0 / 30.0 : 1.0 / 15.0));
    }
}
