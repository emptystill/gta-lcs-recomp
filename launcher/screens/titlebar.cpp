// The title bar the launcher draws itself (the window has no Windows frame): the name, the sheets (Saves, Settings,
// Help) and the window's buttons. It tells the platform where the caption is, so dragging, double-click, snapping
// and the system menu stay Windows' own. macOS: its own traffic lights stay (top left), so none are drawn here.
#include "shell.h"

#include "platform/platform.h"

namespace screens
{
    using namespace render;
    using core::mix;
    using core::withAlpha;
    using core::rgb;
    using ui::label;

    namespace
    {
        void logo(float cx, float cy, float r)
        {
            // the icon (launcher/make_icon.py), small: a red disc ringed in orange with a black outline, a black fedora
            const Color ink = core::rgb(0x07080D);
            circle(cx, cy, r + px(1), ink);
            circle(cx, cy, r, core::rgb(0xF6A623));
            circle(cx, cy, r * 0.84f, ink);
            circle(cx, cy, r * 0.80f, core::rgb(0xB8141E));
            auto P = [&](float u, float v) { return std::pair<float, float>{cx + (u - 0.5f) * r * 2, cy + (v - 0.5f) * r * 2}; };
            auto quad4 = [&](std::pair<float, float> a, std::pair<float, float> b, std::pair<float, float> c, std::pair<float, float> d) {
                const float q[8] = {a.first, a.second, b.first, b.second, c.first, c.second, d.first, d.second};
                quad(q, ink);
            };
            quad4(P(0.31f, 0.66f), P(0.355f, 0.40f), P(0.645f, 0.40f), P(0.69f, 0.66f)); // the crown
            quad4(P(0.355f, 0.40f), P(0.44f, 0.35f), P(0.50f, 0.39f), P(0.50f, 0.40f));  // its pinched top
            quad4(P(0.50f, 0.40f), P(0.50f, 0.39f), P(0.56f, 0.35f), P(0.645f, 0.40f));
            const auto b0 = P(0.17f, 0.64f), b1 = P(0.83f, 0.74f);
            rect({b0.first, b0.second, b1.first - b0.first, b1.second - b0.second}, ink, Corners(r * 0.12f)); // the brim
        }

        bool windowButton(std::string_view idLabel, char32_t icon, Rect r, bool close)
        {
            const ui::Interaction in = ui::interact(ui::id(idLabel), r, false);
            const float hover = in.w->hover;
            if (close)
                rect(r, withAlpha(rgb(0xC42B1C), hover * (1.0f - 0.25f * in.w->press)));
            else
                rect(r, theme::ink(0.07f * hover + 0.05f * in.w->press));
            const Color c = close ? mix(theme::kDim, rgb(0xFFFFFF), hover) : mix(theme::kDim, theme::kText, hover);
            drawIcon(kFontIcons, px(10), r.cx(), r.cy(), platform::window.focused ? c : theme::kFaint, icon);
            return in.clicked;
        }

        bool navButton(std::string_view text, char32_t icon, Rect r, bool active, bool dot, std::vector<Rect> &hits)
        {
            hits.push_back(r);
            const ui::Interaction in = ui::interact(ui::id(text), r, true);
            ui::Widget &w = *in.w;
            if (w.fresh)
                w.t = active ? 1.0f : 0.0f;
            ui::animate(w.t, active ? 1.0f : 0.0f, 16);
            const Rect pillRect = r.inset(0, px(7));
            rect(pillRect, theme::ink(0.06f * w.hover + 0.09f * w.t), px(8));
            const Color c = mix(mix(theme::kDim, theme::kText, w.hover), theme::kText, w.t);
            drawIcon(kFontIcons, px(13), r.x + px(19), r.cy(), c, icon);
            label(kFontBody, px(theme::kSmall), {r.x + px(32), r.y, r.w - px(38), r.h}, c, text);
            if (dot)
            {
                circle(r.x + px(25), r.cy() - px(6), px(3.5f), theme::kNight);
                circle(r.x + px(25), r.cy() - px(6), px(2.5f), theme::kPink);
            }
            if (w.focus > 0.01f)
                outline(pillRect.inset(-px(2)), withAlpha(theme::kPink, w.focus * 0.85f), px(2), px(9));
            return in.clicked;
        }
    }

    float drawTitleBar(Rect window)
    {
        App &app = *shell.app;
        const float h = px(theme::kTitleBar);
        Rect bar{window.x, window.y, window.w, h};
        ui::beginLayer(kLayerSheet, false); // usable under a sheet, not under a dialog
        struct EndLayer
        {
            ~EndLayer() { ui::endLayer(); }
        } endLayer;
        // a hairline under it once something scrolls or a sheet covers the content
        rect({bar.x, bar.bottom() - 1, bar.w, 1}, theme::ink(0.04f));

        const bool focused = platform::window.focused;
        Rect left = bar;
#if defined(__APPLE__)
        // macOS keeps its own window buttons (the traffic lights, top left over our title bar, centred on it by
        // platform_macos.mm's placeWindowButtons: 15 pt in, the last one ending at 69 pt): room for them
        core::cutLeft(left, px(86));
#else
        core::cutLeft(left, px(16));
#endif
        logo(left.x + px(9), bar.cy(), px(8));
        core::cutLeft(left, px(28));
        const float nameW = measure(kFontSemibold, px(12.5f), "Liberty City Stories");
        label(kFontSemibold, px(12.5f), {left.x, bar.y, nameW + px(2), h}, focused ? theme::kText : theme::kDim, "Liberty City Stories");
        label(kFontBody, px(12.5f), {left.x + nameW + px(8), bar.y, px(200), h}, theme::kFaint,
              "PC port  \xC2\xB7  " + app.sourceVersion);

        std::vector<Rect> hits;
        Rect right = bar;
#if defined(__APPLE__)
        const Rect maxR{};
        core::cutRight(right, px(10));
#else
        const float bw = px(46);
        const Rect closeR = core::cutRight(right, bw), maxR = core::cutRight(right, bw), minR = core::cutRight(right, bw);
        hits.push_back(closeR);
        hits.push_back(minR);
        if (windowButton("#close", theme::icon::kClose, closeR, true))
            requestQuit(app);
        if (windowButton("#max", platform::window.maximized ? theme::icon::kRestore : theme::icon::kMaximize, maxR, false))
            platform::toggleMaximize();
        if (windowButton("#min", theme::icon::kMinimize, minR, false))
            platform::minimize();
        core::cutRight(right, px(10));
        rect({right.right(), bar.y + px(14), 1, h - px(28)}, theme::ink(0.08f));
        core::cutRight(right, px(10));
#endif

        struct Nav
        {
            const char *text;
            char32_t icon;
            Sheet sheet;
        };
        const Nav navs[] = {{"Help", theme::icon::kHelp, Sheet::Help},
                            {"Settings", theme::icon::kSettings, Sheet::Settings},
                            {"Saves", theme::icon::kSave, Sheet::Saves},
                            {"Game", theme::icon::kGame, Sheet::Game}};
        for (const Nav &n : navs)
        {
            const float w = measure(kFontBody, px(theme::kSmall), n.text) + px(46);
            const Rect r = core::cutRight(right, w);
            const bool dot = n.sheet == Sheet::Settings && app.updateInfo["newer"].boolean();
            if (navButton(n.text, n.icon, r, shell.sheet == n.sheet, dot, hits))
                openSheet(shell.sheet == n.sheet ? Sheet::None : n.sheet);
            core::cutRight(right, px(4));
        }

        platform::Chrome chrome;
        chrome.caption = bar;
        chrome.maximizeButton = maxR;
        chrome.buttons = hits;
        platform::setChrome(chrome);
        return h;
    }
}
