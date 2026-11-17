// See lcs_prompts.h. Found 2026-11-17 (docs/LCS.md "Button prompts"):
//
// - The expander of `~k~` (0x2F8F58, called by 0x2F91A0 - VCS FUN_003aff20 by xmatch) copies the text of the GXT key
//   C<configuration><ACTION> and turns its ~T~ ~S~ ~X~ ~O~ into the characters 0xE0..0xE3: the font draws them from
//   its own atlas. The font atlas (font1, 256 x 256, a 16 x 16 grid of 16-texel cells from ' ') has the PS2's buttons
//   in its last row: △ □ ✕ ○ ◀ ▼ ▶ in columns 0..6.
// - The pause menu's help bar draws textures of its own: btn_cross_PSP, btn_triangle_PSP ... (RwTextures in the
//   frontend's dictionary, loaded while the menu is up: name at +0x10, the raster at +0).
// The renderer knows a texture by its content: LCS names no uploads by raster (VCS's world renderer does), so the font
// and the help bar's pictures are told by a hash of their decoded pixels (gsHwSetTextureTagger: FNV-1a 64 over w, h and
// the RGBA words; the same under every texture key the game's uploads give them - the font under 4 in one session).
// The hashes were taken from PS2X_HUD_DUMP's dumps of this disc (SLUS-21423). The classifier draws those primitives -
// the font's last-row cells, the pictures whole - from VCS's glyph atlas. Whether the menu is up comes from the
// frontend's textures being loaded (found by name in guest memory by a helper thread: btn_cross_PSP ...).
#include "game/lcs_prompts.h"

#include "game/vcs_prompts.h"
#include "ps2_runtime.h"
#include "runtime/gs/gs_hw.h"
#include "runtime/ps2_control.h"
#include "runtime/ps2_render_worker.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lcs::prompts
{
    namespace
    {
        namespace vp = vcs::prompts;
        constexpr uint32_t kRamMask = 0x01FFFFFFu;
        constexpr uint32_t kPlayerPed = 0x00408E40u; // CWorld::Players[0].m_pPed
        constexpr uint32_t kPedVehicle = 0x534u, kPedInVehicle = 0x538u;

        // the textures by name: the font's atlas and the help bar's buttons (and the control each stands for)
        struct Named
        {
            const char *name;
            int ctl; // -1: the font
        };
        constexpr Named kNamed[] = {
            {"font1", -1},
            {"btn_cross_PSP", vp::Cross},       {"btn_circle_PSP", vp::Circle}, {"btn_triangle_PSP", vp::Triangle},
            {"btn_square_PSP", vp::Square},     {"btn_L_PSP", vp::L1},          {"btn_R_PSP", vp::R1},
            {"btn_up_PSP", vp::DUp},            {"btn_down_PSP", vp::DDown},    {"btn_left_PSP", vp::DLeft},
            {"btn_right_PSP", vp::DRight},      {"btn_select_PSP", vp::Select}, {"btn_start_PSP", vp::Start},
        };
        constexpr size_t kNamedCount = sizeof(kNamed) / sizeof(kNamed[0]);
        // the textures by content (FNV-1a 64 of w, h and the pixels as 32-bit words): tag 1 the font, 100 + control a help-bar picture
        constexpr int kFontTag = 1, kButtonTag = 100;
        struct Known
        {
            uint64_t hash;
            int tag;
        };
        constexpr Known kKnown[] = {
            {0x7de6d9832f7fd725ull, kFontTag},                   // font1, 256 x 256
            {0xdc0124f6f9448dfdull, kButtonTag + vp::Cross},     // btn_cross_PSP
            {0xd666f2e1f93a4eadull, kButtonTag + vp::Triangle},  // btn_triangle_PSP
            {0x103485a08eb03fddull, kButtonTag + vp::Square},    // btn_square_PSP
            {0xebcff16e15bb2318ull, kButtonTag + vp::L1},        // btn_L_PSP
            {0xbca89741f3a19087ull, kButtonTag + vp::R1},        // btn_R_PSP
            {0x946de9e13c05d0adull, kButtonTag + vp::DUp},       // btn_up_PSP
            {0x3b0e2aef3c05d0adull, kButtonTag + vp::DDown},     // btn_down_PSP
            {0xbbc3fd463c05d0adull, kButtonTag + vp::DLeft},     // btn_left_PSP
            {0x65e287793c05d0adull, kButtonTag + vp::DRight},    // btn_right_PSP
        };
        int tagOf(uint32_t w, uint32_t h, const uint32_t *pixels)
        {
            if (!((w == 256 && h == 256) || (w == 16 && h == 16)))
                return 0;
            uint64_t x = 0xcbf29ce484222325ull;
            for (uint32_t v : {w, h})
                x = (x ^ v) * 0x100000001b3ull;
            for (size_t i = 0, n = size_t(w) * h; i < n; ++i) // a word a step: 64 K steps for 256 x 256 (~0.1 ms)
                x = (x ^ pixels[i]) * 0x100000001b3ull;
            for (const Known &k : kKnown)
                if (k.hash == x)
                    return k.tag;
            return 0;
        }
        // the font's last row, columns 0..6
        constexpr int kFontRow[] = {vp::Triangle, vp::Square, vp::Cross, vp::Circle, vp::DLeft, vp::DDown, vp::DRight};

        uint8_t *g_rdram = nullptr;
        bool g_installed = false;

        uint32_t rd32(uint32_t a)
        {
            uint32_t v;
            std::memcpy(&v, g_rdram + (a & kRamMask), 4);
            return v;
        }
        bool inRam(uint32_t a) { return a >= 0x00100000u && a < 0x02000000u; }
        // an RwTexture with this name: raster and dictionary in RAM, the name at +0x10
        bool isTexture(uint32_t tex, const char *name)
        {
            if (!inRam(tex) || !inRam(tex + 0x40))
                return false;
            const size_t n = std::strlen(name);
            return inRam(rd32(tex)) && inRam(rd32(tex + 4)) && std::memcmp(g_rdram + tex + 0x10, name, n + 1) == 0;
        }

        // ---- the finder: a helper thread scans memory for the names; the game thread keeps what still holds ----------
        std::mutex g_foundMutex;
        std::array<std::vector<uint32_t>, kNamedCount> g_found; // RwTexture addresses, per name
        std::atomic<bool> g_scanWanted{true};

        void scanner()
        {
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
                if (!g_scanWanted.exchange(false))
                    continue;
                std::array<std::vector<uint32_t>, kNamedCount> found;
                const uint8_t *ram = g_rdram;
                // the textures live in the streamed heap; a name is at +0x10 of its RwTexture
                for (uint32_t a = 0x00400000u; a + 0x40 < 0x02000000u; a += 4)
                {
                    if (ram[a] != 'b' && ram[a] != 'f')
                        continue;
                    for (size_t i = 0; i < kNamedCount; ++i)
                        if (std::memcmp(ram + a, kNamed[i].name, std::strlen(kNamed[i].name) + 1) == 0 && isTexture(a - 0x10, kNamed[i].name))
                            found[i].push_back(a - 0x10);
                }
                std::lock_guard<std::mutex> lock(g_foundMutex);
                g_found = std::move(found);
            }
        }

        // ---- the GS stage's view -----------------------------------------------------------------------------------------
        struct GsState
        {
            bool on = false;
            std::array<vp::GlyphCell, vp::kCtlCount> cells{};
            std::array<bool, vp::kCtlCount> have{};
        };
        std::shared_ptr<const GsState> g_gs = std::make_shared<GsState>(); // GS stage

        bool classify(int tag, uint64_t, uint32_t tw, uint32_t th, const float *xy, const float *uv, const uint8_t *bright,
                      uint32_t count, GSHwGlyphHit &hit)
        {
            const GsState &g = *g_gs;
            if (count < 2 || !tag || !g.on)
                return false;
            int ctl = -1;
            const bool font = tag == kFontTag;
            if (font)
            {
                if (tw != 256 || th != 256)
                    return false;
                float u = 0.0f, v = 0.0f;
                for (uint32_t i = 0; i < count; ++i)
                {
                    u += uv[2 * i];
                    v += uv[2 * i + 1];
                }
                const int col = static_cast<int>(u / count * 16.0f), row = static_cast<int>(v / count * 16.0f);
                if (row != 15 || col < 0 || col > 6)
                    return false;
                ctl = kFontRow[col];
            }
            else if (tag >= kButtonTag && tag < kButtonTag + vp::kCtlCount)
                ctl = tag - kButtonTag;
            if (ctl < 0 || !g.have[ctl])
                return false;
            const vp::GlyphCell &cell = g.cells[ctl];
            float x0 = xy[0], x1 = xy[0], y0 = xy[1], y1 = xy[1];
            for (uint32_t i = 0; i < count; ++i)
            {
                x0 = std::min(x0, xy[2 * i]);
                x1 = std::max(x1, xy[2 * i]);
                y0 = std::min(y0, xy[2 * i + 1]);
                y1 = std::max(y1, xy[2 * i + 1]);
            }
            const float w = x1 - x0, h = y1 - y0;
            if (w <= 0.0f || h <= 0.0f)
                return false;
            // the glyph at its own shape and the box's height, centred on the box: a wider one (a keycap with a word)
            // grows into the room beside it - text glyphs into the spaces around them (up to 3 x the box), the help
            // bar's pictures up to 1.6 x (below: on the side away from their words) - and past
            // that it is made smaller, centred on the box (a narrower one keeps the box's height)
            const float room = w * (font ? 3.0f : 1.6f);
            float gh = h, want = h * cell.aspect;
            if (want > room)
            {
                gh = room / cell.aspect;
                want = room;
            }
            // text glyphs grow to both sides; the help bar's pictures away from their words - those in the left half
            // ("[Esc] Atrás") to the left, those in the right half ("Objetivo - Sí [Space]") to the right
            float left = -(want - w) * 0.5f, right = (want - w) * 0.5f;
            if (!font && want > w)
            {
                if ((x0 + x1) * 0.5f < 320.0f)
                    left = w - want, right = 0.0f;
                else
                    left = 0.0f, right = want - w;
            }
            const float top = (h - gh) * 0.5f, bottom = -(h - gh) * 0.5f;
            uint8_t lo = 255;
            for (uint32_t i = 0; i < count; ++i)
            {
                const bool isRight = xy[2 * i] > (x0 + x1) * 0.5f, isBottom = xy[2 * i + 1] > (y0 + y1) * 0.5f;
                hit.u[i] = isRight ? cell.u1 : cell.u0;
                hit.v[i] = isBottom ? cell.v1 : cell.v0;
                hit.dx[i] = isRight ? right : left;
                hit.dy[i] = isBottom ? bottom : top;
                lo = std::min(lo, bright[i]);
            }
            hit.shadow = lo < 24; // the text's drop shadow: drawn dark
            return true;
        }

        // ---- the atlas (main thread's helper; the game thread hands it over) -----------------------------------------
        std::mutex g_atlasMutex;
        std::shared_ptr<vp::GlyphAtlas> g_readyAtlas;
        std::atomic<bool> g_building{false};
        std::string g_requested;
        std::shared_ptr<vp::GlyphAtlas> g_atlas; // game thread
        int g_hostFrames = 0;

        // game thread
        std::shared_ptr<const GsState> g_posted;
        int g_postedSet = -1, g_postedCtx = -1;
        std::shared_ptr<vp::GlyphAtlas> g_postedAtlas;
        std::vector<uint32_t> g_postedRasters;
        std::atomic<int> g_lastCtx{-1}, g_lastSet{-1};
        std::atomic<uint32_t> g_lastFonts{0}, g_lastButtons{0};
        uint32_t g_kicks = 0;
    }

    void onHostFrame()
    {
        if (!g_installed || vp::shownSet() == vp::Original || (g_hostFrames++ % 20) != 0 || g_building.load())
            return;
        const int cellH = vp::glyphCellHeight();
        std::vector<std::string> specs = vp::allGlyphSpecs();
        std::string signature = std::to_string(cellH);
        for (const std::string &s : specs)
            signature += "|" + s;
        if (signature == g_requested)
            return;
        g_requested = signature;
        g_building.store(true);
        std::thread([specs = std::move(specs), cellH, signature = std::move(signature)] {
            auto atlas = vp::buildGlyphAtlas(specs, cellH, signature);
            std::fprintf(stderr, "[prompts] glyph atlas: %zu glyphs, %u x %u, %d px high\n", specs.size(), atlas->w, atlas->h, cellH);
            {
                std::lock_guard<std::mutex> lock(g_atlasMutex);
                g_readyAtlas = std::move(atlas);
            }
            g_building.store(false);
        }).detach();
    }

    void onFrameKick(uint8_t *rdram)
    {
        if (!g_installed)
            return;
        g_rdram = rdram;
        {
            std::shared_ptr<vp::GlyphAtlas> ready;
            {
                std::lock_guard<std::mutex> lock(g_atlasMutex);
                ready = std::move(g_readyAtlas);
            }
            if (ready)
            {
                g_atlas = ready;
                const uint32_t w = ready->w, h = ready->h;
                auto pixels = ready->pixels;
                ps2_render_worker::post([w, h, pixels] { ps2_render_worker::runOnGsStage([w, h, pixels] { gsHwSetGlyphAtlas(w, h, pixels); }); });
            }
        }
        const int set = vp::shownSet();
        // the textures still where they were found (a name moved or gone: look again)
        std::vector<uint32_t> fonts;
        std::vector<std::pair<uint32_t, int>> buttons;
        {
            std::lock_guard<std::mutex> lock(g_foundMutex);
            bool stale = false;
            for (size_t i = 0; i < kNamedCount; ++i)
                for (uint32_t tex : g_found[i])
                {
                    if (!isTexture(tex, kNamed[i].name))
                    {
                        stale = true;
                        continue;
                    }
                    if (kNamed[i].ctl < 0)
                        fonts.push_back(rd32(tex));
                    else
                        buttons.emplace_back(rd32(tex), kNamed[i].ctl);
                }
            // the menu's textures come and go with it, the font with a load: look again now and then
            if (stale || fonts.empty() || (++g_kicks % 120) == 0)
                g_scanWanted.store(true);
        }
        const uint32_t ped = rd32(kPlayerPed);
        const bool inVehicle = inRam(ped) && inRam(rd32(ped + kPedVehicle)) && (rd32(ped + kPedInVehicle) & 0xFFu) != 0;
        const int ctx = !buttons.empty() ? vp::Menu : inVehicle ? vp::Vehicle : vp::Foot;
        g_lastCtx.store(ctx);
        g_lastSet.store(set);
        g_lastFonts.store(static_cast<uint32_t>(fonts.size()));
        g_lastButtons.store(static_cast<uint32_t>(buttons.size()));

        std::vector<uint32_t> rasters = fonts;
        for (const auto &b : buttons)
            rasters.push_back(b.first ^ (static_cast<uint32_t>(b.second) << 26));
        const bool off = set == vp::Original || !g_atlas;
        if (!off && set == g_postedSet && ctx == g_postedCtx && g_atlas == g_postedAtlas && rasters == g_postedRasters)
            return;
        if (off && g_postedSet == vp::Original)
            return;
        auto state = std::make_shared<GsState>();
        if (!off)
        {
            state->on = true;
            for (int c = 0; c < vp::kCtlCount; ++c)
            {
                const auto it = g_atlas->cells.find(vp::glyphSpec(set, ctx, c));
                state->have[c] = it != g_atlas->cells.end();
                if (state->have[c])
                    state->cells[c] = it->second;
            }
        }
        g_postedSet = off ? vp::Original : set;
        g_postedCtx = ctx;
        g_postedAtlas = g_atlas;
        g_postedRasters = std::move(rasters);
        std::shared_ptr<const GsState> shared = std::move(state);
        ps2_render_worker::post([shared] { ps2_render_worker::runOnGsStage([shared] { g_gs = shared; }); });
    }

    void install(PS2Runtime &runtime)
    {
        g_rdram = runtime.memory().getRDRAM();
        const char *gs = std::getenv("PS2X_GS");
        if (!(gs && std::strcmp(gs, "hw") == 0))
        {
            std::fprintf(stderr, "[prompts] button prompts off (not the hardware renderer)\n");
            return;
        }
        gsHwSetTextureTagger(&tagOf);
        gsHwSetTextureGlyphClassifier(&classify);
        g_installed = true;
        std::thread(scanner).detach();
        // prompts -> ok set= ctx= fonts= buttons= drawn=
        ps2control::addCommand("prompts", [](const std::string &, ps2control::Reply reply) {
            char buf[200];
            const int c = g_lastCtx.load();
            std::snprintf(buf, sizeof(buf), "ok set=%d ctx=%s fonts=%u buttons=%u drawn=%llu", g_lastSet.load(),
                          c == vp::Menu ? "menu" : c == vp::Vehicle ? "vehicle" : c == vp::Foot ? "foot" : "?", g_lastFonts.load(),
                          g_lastButtons.load(), static_cast<unsigned long long>(gsHwGlyphDraws()));
            reply(buf);
        });
        std::fprintf(stderr, "[prompts] button prompts ready (Enhancements > Button prompts)\n");
    }
}
