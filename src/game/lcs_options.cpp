// See lcs_options.h. The menu's table and code, found 2026-11-17 (docs/LCS.md "The pause menu's own table"):
//
// - The screen table at 0x393A30: 0x116 bytes a screen (title key, then 15 rows of 0x12 bytes: u16 action, char key[8],
//   u8 0, u8 the screen, u16 x, u16 y, u16 alignment). Display is screen 5, Audio 4.
// - Drawing (FUN_00339fe0): a row's label is CText::Get(table, row key) (0x332E68); for action 0x0B (Subtitles) the
//   value is CText::Get of the key at 0x3D8D50 or 0x3D8DA8 (off / on), right after - so a row of ours is an 0x0B row
//   whose label key is ours, and the Get that follows it returns our value.
// - Input (FUN_00340b80, the manager in a0): +0x588 the screen, +0x58C the selected row. Left / right / Cross on an 0x0B
//   row would turn the subtitles: on our rows they are taken from the pad first (filterPad).
#include "game/lcs_options.h"

#include "game/vcs_framerate.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "settings/settings.h"
#include "ui/i18n.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace lcs::options
{
    namespace
    {
        constexpr uint32_t kRamMask = 0x01FFFFFFu;
        constexpr uint32_t kTextGet = 0x00332E68u;     // CText::Get(table, key)
        constexpr uint32_t kMenuInput = 0x00340B80u;   // the menu's input (the manager in a0)
        constexpr uint32_t kTable = 0x00393A30u, kScreenSize = 0x116u, kRowSize = 0x12u;
        constexpr int kAudio = 4, kDisplay = 5;
        constexpr uint32_t kValueOff = 0x003D8D50u, kValueOn = 0x003D8DA8u; // the Subtitles row's value keys
        constexpr uint32_t kMenuUp = 0x003D9CB4u, kInPage = 0x00634BC4u;    // (lcs_frontend.cpp)
        constexpr uint16_t kActionSubtitles = 0x0B;
        constexpr uint32_t kStrings = 0x0007C000u; // our texts (EE kernel RAM no one uses; lcs_mouseaim.cpp)
        constexpr uint16_t kLeft = 0x0080, kRight = 0x0020, kCross = 0x4000;

        struct Choice
        {
            int value;
            const char *en, *es;
        };
        struct Row
        {
            const char *key;     // GXT key (7 characters at most)
            const char *setting; // settings key
            const char *en, *es; // the label
            std::vector<Choice> choices; // empty: an integer 0..100 in steps of 10, shown as a percentage
        };
        const std::vector<Row> &rows()
        {
            static const std::vector<Row> r = {
                {"LCSASP", "display.aspect", "ASPECT RATIO:", "RELACIÓN DE ASPECTO:",
                 {{43, "4:3", "4:3"}, {1610, "16:10", "16:10"}, {169, "16:9", "16:9"}, {219, "21:9", "21:9"},
                  {329, "32:9", "32:9"}, {0, "MATCH WINDOW", "SEGÚN LA VENTANA"}}},
                {"LCSHUD", "display.hud", "WIDESCREEN HUD:", "HUD PANORÁMICO:",
                 {{0, "KEEP 4:3", "MANTENER 4:3"}, {2, "SCREEN EDGES", "BORDES DE LA PANTALLA"}, {1, "STRETCH", "ESTIRAR"}}},
                {"LCSFPS", "display.fps", "FRAME RATE CAP:", "LÍMITE DE FPS:",
                 {{30, "30 (ORIGINAL)", "30 (ORIGINAL)"}, {60, "60", "60"}, {100, "100", "100"}, {120, "120", "120"},
                  {144, "144", "144"}, {165, "165", "165"}, {240, "240", "240"},
                  {vcs::kCapRefresh, "MONITOR REFRESH", "REFRESCO DEL MONITOR"}, {vcs::kCapUnlimited, "UNLIMITED", "SIN LÍMITE"}}},
                {"LCSVOL", "audio.volume", "MASTER VOLUME:", "VOLUMEN GENERAL:", {}},
            };
            return r;
        }

        uint8_t *g_rdram = nullptr;
        bool g_on = true;
        PS2Runtime::RecompiledFunction g_origGet = nullptr, g_origInput = nullptr;
        std::atomic<uint32_t> g_manager{0};
        int g_lastLabel = -1; // the row of ours whose label was just asked for (game thread)
        uint16_t g_prevButtons = 0;
        float g_prevLx = 0.0f;
        std::mutex g_opsMutex;
        std::vector<std::pair<int, int>> g_ops; // (row, step) for the main thread

        uint32_t rd32(uint32_t a)
        {
            uint32_t v = 0;
            std::memcpy(&v, g_rdram + (a & kRamMask), 4);
            return v;
        }
        uint16_t rd16(uint32_t a)
        {
            uint16_t v = 0;
            std::memcpy(&v, g_rdram + (a & kRamMask), 2);
            return v;
        }
        uint32_t rowAddr(int screen, int row) { return kTable + uint32_t(screen) * kScreenSize + 8u + uint32_t(row) * kRowSize; }
        std::string rowKey(int screen, int row)
        {
            char key[9] = {};
            std::memcpy(key, g_rdram + ((rowAddr(screen, row) + 2u) & kRamMask), 8);
            return key;
        }
        int ourRow(const char *key)
        {
            const auto &r = rows();
            for (size_t i = 0; i < r.size(); ++i)
                if (std::strncmp(key, r[i].key, 8) == 0)
                    return static_cast<int>(i);
            return -1;
        }

        // the game's text: UTF-16 in its font's code page - capitals, and Latin-1's accented capitals 0x40 lower
        // (Á 0x81, Í 0x8D, Ñ 0x91: SPANISH text's own, read from the disc's GXT)
        void writeText(uint32_t at, const std::string &utf8)
        {
            std::vector<uint16_t> out;
            for (size_t i = 0; i < utf8.size() && out.size() < 120;)
            {
                uint32_t c = static_cast<unsigned char>(utf8[i]);
                if (c >= 0xC0 && c < 0xE0 && i + 1 < utf8.size())
                {
                    c = ((c & 0x1F) << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3F);
                    i += 2;
                }
                else
                    ++i;
                if (c >= 'a' && c <= 'z')
                    c -= 32;
                else if (c >= 0xE0 && c <= 0xFE && c != 0xF7)
                    c -= 0x20;
                if (c >= 0xC0 && c <= 0xDE)
                    c -= 0x40;
                else if (c >= 0x80)
                    c = '?';
                out.push_back(static_cast<uint16_t>(c));
            }
            out.push_back(0);
            std::memcpy(g_rdram + (at & kRamMask), out.data(), out.size() * 2);
        }

        std::string valueText(const Row &r)
        {
            const int v = settings::value(r.setting);
            const bool es = ui::i18n::spanish();
            if (r.choices.empty())
                return std::to_string(v) + "%";
            for (const Choice &c : r.choices)
                if (c.value == v)
                    return es ? c.es : c.en;
            return std::to_string(v);
        }

        int stepped(const Row &r, int dir)
        {
            const int v = settings::value(r.setting);
            if (r.choices.empty())
            {
                const int nv = (v / 10 + dir) * 10;
                return nv < 0 ? 0 : nv > 100 ? 100 : nv;
            }
            const int n = static_cast<int>(r.choices.size());
            int at = 0;
            for (int i = 0; i < n; ++i)
                if (r.choices[size_t(i)].value == v)
                    at = i;
            return r.choices[size_t((at + dir + n) % n)].value;
        }

        // CText::Get: our labels, and the value right after one of them
        PS2_GUEST_CC void textGet(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t keyAddr = GPR_U32(ctx, 5);
            const char *key = reinterpret_cast<const char *>(rdram + (keyAddr & kRamMask));
            if (key[0] == 'L' && key[1] == 'C' && key[2] == 'S')
            {
                const int i = ourRow(key);
                if (i >= 0)
                {
                    const Row &r = rows()[size_t(i)];
                    const uint32_t at = kStrings + uint32_t(i) * 0x200u;
                    writeText(at, ui::i18n::spanish() ? r.es : r.en);
                    g_lastLabel = i;
                    SET_GPR_U32(ctx, 2, at);
                    ctx->pc = GPR_U32(ctx, 31);
                    return;
                }
            }
            if (g_lastLabel >= 0 && (keyAddr == kValueOff || keyAddr == kValueOn))
            {
                const int i = g_lastLabel;
                g_lastLabel = -1;
                const uint32_t at = kStrings + uint32_t(i) * 0x200u + 0x100u;
                writeText(at, valueText(rows()[size_t(i)]));
                SET_GPR_U32(ctx, 2, at);
                ctx->pc = GPR_U32(ctx, 31);
                return;
            }
            g_lastLabel = -1;
            g_origGet(rdram, ctx, runtime);
        }

        PS2_GUEST_CC void menuInput(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            g_manager.store(GPR_U32(ctx, 4), std::memory_order_relaxed);
            g_origInput(rdram, ctx, runtime);
        }

        // the row of ours the menu has selected, or -1
        int selectedRow()
        {
            const uint32_t m = g_manager.load(std::memory_order_relaxed);
            if (!g_rdram || !m || rd32(kMenuUp) != 1 || rd32(kInPage) == 0)
                return -1;
            const int screen = static_cast<int>(rd32(m + 0x588u)), row = static_cast<int>(rd32(m + 0x58Cu));
            if ((screen != kDisplay && screen != kAudio) || row < 0 || row >= 15)
                return -1;
            if (rd16(rowAddr(screen, row)) != kActionSubtitles)
                return -1;
            return ourRow(rowKey(screen, row).c_str());
        }

        struct RowBytes
        {
            uint8_t b[0x12];
        };
        RowBytes readRow(int screen, int row)
        {
            RowBytes r;
            std::memcpy(r.b, g_rdram + (rowAddr(screen, row) & kRamMask), sizeof(r.b));
            return r;
        }
        RowBytes atY(RowBytes r, uint16_t y)
        {
            std::memcpy(r.b + 0x0E, &y, 2);
            return r;
        }
        RowBytes ours(const char *key, int screen, uint16_t y)
        {
            RowBytes r{};
            const uint16_t action = kActionSubtitles, x = 0xF0, align = 2;
            std::memcpy(r.b, &action, 2);
            std::strncpy(reinterpret_cast<char *>(r.b + 2), key, 8);
            r.b[0x0B] = static_cast<uint8_t>(screen);
            std::memcpy(r.b + 0x0C, &x, 2);
            std::memcpy(r.b + 0x0E, &y, 2);
            std::memcpy(r.b + 0x10, &align, 2);
            return r;
        }
        void writeRows(int screen, const std::vector<RowBytes> &list)
        {
            for (size_t i = 0; i < list.size(); ++i)
                std::memcpy(g_rdram + (rowAddr(screen, int(i)) & kRamMask), list[i].b, sizeof(RowBytes));
        }
    }

    void install(PS2Runtime &runtime)
    {
        if (const char *e = std::getenv("LCS_MENU_ROWS"); e && e[0] == '0')
            g_on = false;
        if (!g_on)
            return;
        g_rdram = runtime.memory().getRDRAM();
        g_origGet = runtime.lookupFunction(kTextGet);
        g_origInput = runtime.lookupFunction(kMenuInput);
        if (!g_origGet || !g_origInput)
        {
            g_on = false;
            std::fprintf(stderr, "[menu-rows] the menu's functions were not found: off\n");
            return;
        }
        runtime.replaceFunction(kTextGet, &textGet);
        runtime.replaceFunction(kMenuInput, &menuInput);
        std::fprintf(stderr, "[menu-rows] our settings in the pause menu (Display, Audio)\n");
    }

    void onFrameKick(uint8_t *rdram)
    {
        if (!g_on || !rdram)
            return;
        g_rdram = rdram;
        // Display: Brightness, Subtitles, [Aspect ratio], Radar mode, HUD mode, [Widescreen HUD], [Frame rate cap],
        // Screen position - the PS2's rows 20 apart from y 40, ours where Widescreen was and before Screen position
        if (rowKey(kDisplay, 2) == "FED_WIS" && rowKey(kDisplay, 5) == "FED_POS")
        {
            const RowBytes bri = readRow(kDisplay, 0), sub = readRow(kDisplay, 1), rdr = readRow(kDisplay, 3),
                           hud = readRow(kDisplay, 4), pos = readRow(kDisplay, 5);
            writeRows(kDisplay, {bri, sub, ours("LCSASP", kDisplay, 80), rdr, hud, ours("LCSHUD", kDisplay, 140),
                                 ours("LCSFPS", kDisplay, 160), atY(pos, 180)});
            std::fprintf(stderr, "[menu-rows] Display: aspect ratio, widescreen HUD, frame rate cap\n");
        }
        // Audio: Music, SFX, [Master volume] in the gap at y 80, the radio station
        if (rowKey(kAudio, 2) == "FEA_RSS" && rowKey(kAudio, 3).empty())
        {
            const RowBytes mus = readRow(kAudio, 0), sfx = readRow(kAudio, 1), rss = readRow(kAudio, 2);
            writeRows(kAudio, {mus, sfx, ours("LCSVOL", kAudio, 80), rss});
            std::fprintf(stderr, "[menu-rows] Audio: master volume\n");
        }
    }

    void filterPad(uint16_t &buttons, float &lx, float &ly)
    {
        (void)ly;
        if (!g_on)
            return;
        const uint16_t was = g_prevButtons;
        const float wasLx = g_prevLx;
        g_prevButtons = buttons;
        g_prevLx = lx;
        const int row = selectedRow();
        if (row < 0)
            return;
        int dir = 0;
        auto pressed = [&](uint16_t b) { return (buttons & b) && !(was & b); };
        if (pressed(kLeft) || (lx < -0.5f && wasLx >= -0.5f))
            dir = -1;
        else if (pressed(kRight) || pressed(kCross) || (lx > 0.5f && wasLx <= 0.5f))
            dir = 1;
        // the game never sees them on our rows (they would turn the subtitles)
        buttons &= static_cast<uint16_t>(~(kLeft | kRight | kCross));
        lx = 0.0f;
        if (dir)
        {
            std::lock_guard<std::mutex> lock(g_opsMutex);
            g_ops.push_back({row, dir});
        }
    }

    void onHostFrame()
    {
        std::vector<std::pair<int, int>> ops;
        {
            std::lock_guard<std::mutex> lock(g_opsMutex);
            ops.swap(g_ops);
        }
        for (const auto &[row, dir] : ops)
        {
            const Row &r = rows()[size_t(row)];
            settings::set(r.setting, stepped(r, dir));
        }
    }
}
