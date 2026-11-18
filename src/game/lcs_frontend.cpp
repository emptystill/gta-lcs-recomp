// See lcs_frontend.h. The menu's state, found 2026-11-17 by RAM dumps across its pages (docs/LCS.md):
//
// - 0x634610 (u32): the page shown (0 until the menu is first opened). The tab bar's two rows: 1 Map, 2 Brief, 3 Game, 4 Stats;
//   5 Controls, 6 Audio, 7 Display. Right / left step along a row, down / up to the other row.
// - 0x634BC4 (u32): 0 while the tab bar has the focus, 1 inside the page (Cross on a tab).
// - 0x3D9CB4 (u32): 1 while the menu is up, 0 in play (the page keeps its value after the menu closes): found by dumps
//   before, during and after the menu, twice. Start or Triangle on the tab bar close it.
// - Where the tabs are drawn (the 640 x 448 picture, the menu's 4:3 area): measured on screenshots of the Spanish
//   game - the text's widths are the language's, so the boxes are generous.
#include "game/lcs_frontend.h"

#include "game/lcs_options.h"
#include "game/vcs_frontend.h"
#include "platform/platform.h"
#include "ps2_runtime.h"
#include "render/presenter.h"
#include "runtime/ps2_control.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>

namespace lcs::frontend
{
    namespace
    {
        constexpr uint32_t kRamMask = 0x01FFFFFFu;
        constexpr uint32_t kPage = 0x00634610u, kInPage = 0x00634BC4u;
        constexpr uint32_t kMenuUp = 0x003D9CB4u; // 1 while the menu is up (the page above keeps its value after)
        constexpr int kMapPage = 1;
        // the pad word as game/vcs_input.cpp builds it (pressed = 1)
        constexpr uint16_t kUp = 0x0010, kRight = 0x0020, kDown = 0x0040, kLeft = 0x0080, kL1 = 0x0400, kR1 = 0x0800,
                           kTriangle = 0x1000, kCross = 0x4000, kStart = 0x0008;

        struct Box
        {
            int page;
            float x0, y0, x1, y1;
        };
        // the tab bar (Spanish widths; a few pixels' margin)
        constexpr Box kTabs[] = {
            {1, 118, 378, 176, 404}, {2, 178, 378, 312, 404}, {3, 314, 378, 378, 404}, {4, 380, 378, 504, 404},
            {5, 180, 405, 281, 434}, {6, 283, 405, 352, 434}, {7, 354, 405, 444, 434},
        };
        int rowOf(int page) { return page >= 5 ? 1 : 0; }
        int colOf(int page) { return page >= 5 ? page - 5 : page - 1; }

        uint8_t *g_rdram = nullptr;
        bool g_mouseOn = true;
        uint32_t rd32(uint32_t a)
        {
            uint32_t v = 0;
            if (g_rdram)
                std::memcpy(&v, g_rdram + (a & kRamMask), 4);
            return v;
        }
        int page() { return static_cast<int>(rd32(kPage)); }
        bool inPage() { return rd32(kInPage) != 0; }
        bool menuOpen() { return g_rdram && rd32(kMenuUp) == 1 && page() >= 1 && page() <= 7; }

        // the cursor over the menu's 640 x 448 picture (main thread), or injected by `menumouse`
        std::atomic<bool> g_cursorValid{false};
        std::atomic<float> g_cursorX{0.0f}, g_cursorY{0.0f};
        std::atomic<bool> g_injected{false};
        std::atomic<int> g_injectClick{0}; // 1 left, 2 right, 3 wheel up, 4 wheel down

        // ---- game thread: the buttons the menu gets ----------------------------------------------------------------
        using Clock = std::chrono::steady_clock;
        struct State
        {
            bool open = false;
            bool owned[6] = {}; // Escape, Enter, left, right, wheel up, wheel down: held from the menu
            bool was[6] = {};
            uint16_t escButton = 0;
            std::deque<uint16_t> queue; // pulses to send
            uint16_t pulse = 0;
            Clock::time_point pulseEnd{}, gapEnd{};
            int target = 0, targetSteps = 0;
            float lastX = -1.0f, lastY = -1.0f;
        } g;
        constexpr int kKeys[6] = {platform::KEY_ESCAPE, platform::KEY_ENTER, platform::KEY_MOUSE_LEFT, platform::KEY_MOUSE_RIGHT,
                                  platform::KEY_WHEEL_UP, platform::KEY_WHEEL_DOWN};
        bool isMouseKey(int i) { return i >= 2; }

        const Box *tabAt(float x, float y)
        {
            for (const Box &b : kTabs)
                if (x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1)
                    return &b;
            return nullptr;
        }

        // one D-pad step toward the target tab (on the tab bar), 0 when there
        uint16_t stepToward(int target)
        {
            const int p = page();
            if (p == target || p < 1 || p > 7)
                return 0;
            if (rowOf(p) != rowOf(target))
                return rowOf(target) > rowOf(p) ? kDown : kUp;
            return colOf(target) > colOf(p) ? kRight : kLeft;
        }

        uint16_t buttons(uint16_t user, bool sticks)
        {
            const auto now = Clock::now();
            const bool open = menuOpen();
            bool down[6], edge[6];
            for (int i = 0; i < 6; ++i)
            {
                bool d = ps2control::keyDown(kKeys[i]);
                if (isMouseKey(i) && g_injected.load(std::memory_order_relaxed))
                {
                    const int c = g_injectClick.load(std::memory_order_relaxed);
                    d = (i == 2 && c == 1) || (i == 3 && c == 2) || (i == 4 && c == 3) || (i == 5 && c == 4);
                }
                if (isMouseKey(i) && !g_mouseOn)
                    d = false;
                down[i] = d;
                edge[i] = d && !g.was[i];
                g.was[i] = d;
                // a key pressed while the menu is up is the menu's until let go (Escape is Start's key outside it)
                if (!d)
                    g.owned[i] = false;
                else if (open && edge[i])
                    g.owned[i] = true;
            }
            if (!open)
            {
                g.open = false; // (`owned` / `was` stay: a key held from the menu stays its own)
                g.queue.clear();
                g.pulse = 0;
                g.target = 0;
                return 0;
            }
            if (!g.open)
            {
                g.open = true;
                g.lastX = g.lastY = -1.0f;
            }
            uint16_t out = 0;
            // the keys: Enter is Cross while held; Escape is Triangle in a page, Start on the tab bar (it closes)
            if (down[1] && g.owned[1])
                out |= kCross;
            if (edge[0] && g.owned[0])
                g.escButton = inPage() ? kTriangle : kStart;
            if (down[0] && g.owned[0])
                out |= g.escButton;

            // the player's own buttons or sticks: the mouse waits
            if (user || sticks)
            {
                g.queue.clear();
                g.target = 0;
            }
            else if (g_mouseOn && g_cursorValid.load(std::memory_order_relaxed))
            {
                const float x = g_cursorX.load(std::memory_order_relaxed), y = g_cursorY.load(std::memory_order_relaxed);
                const bool moved = std::fabs(x - g.lastX) + std::fabs(y - g.lastY) > 1.5f;
                g.lastX = x;
                g.lastY = y;
                const Box *tab = tabAt(x, y);
                if (edge[2]) // left click
                {
                    if (tab)
                    {
                        if (inPage())
                            g.queue.push_back(kTriangle); // out of the page first
                        if (tab->page == page() && !inPage())
                            g.queue.push_back(kCross);
                        else
                        {
                            g.target = tab->page;
                            g.targetSteps = 0;
                        }
                    }
                    else if (y < 376.0f)
                        g.queue.push_back(kCross); // into the page / select in it
                }
                else if (tab && moved && !inPage() && g.queue.empty())
                {
                    g.target = tab->page; // hovering a tab moves the focus there
                    g.targetSteps = 0;
                }
                if (edge[3]) // right click: back
                    g.queue.push_back(inPage() ? kTriangle : kStart);
                if (edge[4] || edge[5]) // the wheel: the map zooms, a page's selection moves
                {
                    const bool upNotch = edge[4];
                    if (page() == kMapPage && inPage())
                        g.queue.push_back(upNotch ? kR1 : kL1);
                    else if (inPage())
                        g.queue.push_back(upNotch ? kUp : kDown);
                }
            }
            // the pulses: pressed 60 ms, then 60 ms up, so the menu sees each one
            if (g.pulse)
            {
                if (now < g.pulseEnd)
                    return out | g.pulse;
                g.pulse = 0;
                g.gapEnd = now + std::chrono::milliseconds(60);
            }
            if (now < g.gapEnd)
                return out;
            uint16_t next = 0;
            if (!g.queue.empty())
            {
                next = g.queue.front();
                g.queue.pop_front();
            }
            else if (g.target && !inPage())
            {
                next = stepToward(g.target);
                if (!next || ++g.targetSteps > 12)
                {
                    g.target = 0;
                    next = 0;
                }
            }
            if (next)
            {
                g.pulse = next;
                g.pulseEnd = now + std::chrono::milliseconds(60);
                out |= next;
            }
            return out;
        }

        // ---- the delegate ---------------------------------------------------------------------------------------------
        bool dOpen() { return menuOpen(); }
        void dHostFrame()
        {
            lcs::options::onHostFrame(); // what our menu rows changed (main thread)
            if (g_injected.load(std::memory_order_relaxed))
                return;
            int w = 0, h = 0;
            platform::windowSize(w, h);
            float mx = 0.0f, my = 0.0f;
            bool over = g_mouseOn && ps2control::physicalInputOn() && platform::mousePosition(mx, my) && w > 0 && h > 0;
            if (over)
            {
                // the menu's 640 x 448 drawing fills the 4:3 area of the game's frame (as VCS's: vcs_frontend.cpp)
                float x0 = 0.0f, y0 = 0.0f, fw = 0.0f, fh = 0.0f;
                present::fitRect(4.0f / 3.0f, present::displayAspect(true), static_cast<float>(w), static_cast<float>(h), x0, y0, fw, fh);
                if (fw > 0.0f && fh > 0.0f)
                {
                    g_cursorX.store((mx - x0) / fw * 640.0f, std::memory_order_relaxed);
                    g_cursorY.store((my - y0) / fh * 448.0f, std::memory_order_relaxed);
                }
                else
                    over = false;
            }
            g_cursorValid.store(over, std::memory_order_relaxed);
        }
        uint16_t dPadButtons(uint16_t user, bool sticks, bool blocked) { return blocked ? 0 : buttons(user, sticks); }
        bool dOwnsKey(int key)
        {
            for (int i = 0; i < 6; ++i)
                if (kKeys[i] == key)
                    return g.owned[i] || (menuOpen() && (g_mouseOn || !isMouseKey(i)));
            return false;
        }
        const vcs::frontend::Delegate kDelegate{&dOpen, &dHostFrame, &dPadButtons, &dOwnsKey, &lcs::options::filterPad};
    }

    void install(PS2Runtime &runtime)
    {
        g_rdram = runtime.memory().getRDRAM();
        if (const char *e = std::getenv("LCS_MENU_MOUSE"); e && e[0] == '0')
            g_mouseOn = false;
        vcs::frontend::setDelegate(&kDelegate);
        // menustate -> ok open= page= inpage=
        ps2control::addCommand("menustate", [](const std::string &, ps2control::Reply reply) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "ok open=%d page=%d inpage=%d", menuOpen() ? 1 : 0, page(), inPage() ? 1 : 0);
            reply(buf);
        });
        // menumouse X Y [left|right|wheelup|wheeldown|none] -> the cursor at X, Y of the menu's 640 x 448 (tests); off
        // -> the real mouse again
        ps2control::addCommand("menumouse", [](const std::string &args, ps2control::Reply reply) {
            if (args == "off")
            {
                g_injected.store(false);
                return reply("ok");
            }
            float x = 0.0f, y = 0.0f;
            char what[16] = "none";
            if (std::sscanf(args.c_str(), "%f %f %15s", &x, &y, what) < 2)
                return reply("error usage: menumouse X Y [left|right|wheelup|wheeldown|none] | off");
            g_cursorX.store(x);
            g_cursorY.store(y);
            g_cursorValid.store(true);
            g_injected.store(true);
            const std::string w = what;
            g_injectClick.store(w == "left" ? 1 : w == "right" ? 2 : w == "wheelup" ? 3 : w == "wheeldown" ? 4 : 0);
            reply("ok");
        });
        std::fprintf(stderr, "[menu] the pause menu with Enter / Escape%s\n", g_mouseOn ? " and the mouse" : "");
    }
}
