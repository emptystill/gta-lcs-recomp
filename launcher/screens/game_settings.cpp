// The Game sheet: the game's own settings (the F2 menu's) in the launcher, from lcs.exe's description of them
// (model/game_settings.h): its pages as tabs, each page's sections and rows - a switch, a choice (segments, or arrows
// for long lists), a slider, a key to bind, a button - and the hovered row's help at the bottom. Changes go to lcs.ini
// at once and apply when the game starts; while the game runs the sheet only shows them (the game owns lcs.ini then).
#include "shell.h"

#include "core/i18n.h"
#include "platform/platform.h"

#include <algorithm>
#include <cstdio>

namespace screens
{
    using namespace render;
    using core::mix;
    using core::withAlpha;
    using launcher::GameSettings;
    using launcher::Json;
    using ui::ButtonStyle;

    namespace
    {
        struct State
        {
            std::string page;       // the page shown (its English name)
            std::string help;       // the hovered row's help
            std::string capturing;  // the key setting waiting for a key ("" none)
            std::string confirming; // an action waiting for its second click
            double confirmUntil = 0;
        };
        State S;

        // English capitals, and Spanish ones (Latin-1's lower case letters in UTF-8: C3 A0..BE but F7)
        std::string upper(std::string s)
        {
            for (size_t i = 0; i < s.size(); ++i)
            {
                unsigned char c = static_cast<unsigned char>(s[i]);
                if (c >= 'a' && c <= 'z')
                    s[i] = static_cast<char>(c - 32);
                else if (c == 0xC3 && i + 1 < s.size())
                {
                    const unsigned char d = static_cast<unsigned char>(s[i + 1]);
                    if (d >= 0xA0 && d <= 0xBE && d != 0xB7)
                        s[i + 1] = static_cast<char>(d - 0x20);
                    ++i;
                }
            }
            return s;
        }

        // the launcher's keys (Windows virtual-key codes on every platform) as the game's (GLFW's numbers)
        int gameKey(int vk)
        {
            if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
                return vk;
            if (vk >= 0x70 && vk <= 0x7B)
                return 290 + (vk - 0x70); // F1..F12
            if (vk >= 0x60 && vk <= 0x69)
                return 320 + (vk - 0x60); // keypad 0..9
            switch (vk)
            {
            case 0x20: return 32;   // space
            case 0x0D: return 257;  // enter
            case 0x09: return 258;  // tab
            case 0x08: return 259;  // backspace
            case 0x2D: return 260;  // insert
            case 0x2E: return 261;  // delete
            case 0x27: return 262;  // right
            case 0x25: return 263;  // left
            case 0x28: return 264;  // down
            case 0x26: return 265;  // up
            case 0x21: return 266;  // page up
            case 0x22: return 267;  // page down
            case 0x24: return 268;  // home
            case 0x23: return 269;  // end
            case 0x14: return 280;  // caps lock
            case 0x6E: return 330;  // keypad .
            case 0x6F: return 331;  // keypad /
            case 0x6A: return 332;  // keypad *
            case 0x6D: return 333;  // keypad -
            case 0x6B: return 334;  // keypad +
            case 0x10: case 0xA0: case 0xA1: return 340; // shift (the bindings take both sides)
            case 0x11: case 0xA2: case 0xA3: return 341; // control
            case 0x12: case 0xA4: case 0xA5: return 342; // alt
            case 0xBA: return 59;   // ;
            case 0xBB: return 61;   // =
            case 0xBC: return 44;   // ,
            case 0xBD: return 45;   // -
            case 0xBE: return 46;   // .
            case 0xBF: return 47;   // /
            case 0xC0: return 96;   // `
            case 0xDB: return 91;   // [
            case 0xDC: return 92;   // backslash
            case 0xDD: return 93;   // ]
            case 0xDE: return 39;   // '
            default: return 0;
            }
        }

        // what was pressed this frame for a binding: a key, a mouse button or a wheel notch (the game's codes);
        // -1 nothing, -2 Escape (cancel)
        int capturedKey()
        {
            const platform::Input &in = platform::input;
            if (in.key(platform::kKeyEscape))
                return -2;
            for (int vk = 1; vk < 256; ++vk)
                if (in.keyPressed[vk])
                    if (const int k = gameKey(vk))
                        return k;
            static const int kMouse[3] = {400, 401, 402};
            for (int b = 0; b < 3; ++b)
                if (in.pressed[b])
                    return kMouse[b];
            if (in.wheel > 0)
                return 405;
            if (in.wheel < 0)
                return 406;
            return -1;
        }

        std::string formatted(const Json &s, int v)
        {
            char text[64];
            std::snprintf(text, sizeof(text), s["format"].str("%d").c_str(), v);
            return text;
        }

        // binds a key the way the game's menu does: keys of a group are exclusive - an optional setting that had it
        // loses it, a required one (the menu keys) refuses
        void bind(App &app, const Json &s, int key)
        {
            GameSettings &g = app.gameSettings;
            const std::string own = s["key"].str(), group = s["group"].str();
            const bool optional = s["optional"].boolean();
            if (key != 0 && !group.empty())
            {
                const Json &all = g.settings();
                for (size_t i = 0; i < all.size(); ++i)
                {
                    const Json &o = all[i];
                    if (o["key"].str() == own || o["type"].str() != "key" || o["group"].str().empty())
                        continue;
                    if ((o["group"].str() == group || !o["optional"].boolean() || !optional) && g.value(o["key"].str()) == key)
                    {
                        if (!o["optional"].boolean() || !optional)
                        {
                            toast(g.keyName(key) + " is already \"" + GameSettings::text(o["label"]) + "\" (" +
                                      GameSettings::text(o["page"]) + ").",
                                  theme::kRed);
                            return;
                        }
                        g.set(o["key"].str(), 0);
                    }
                }
            }
            g.set(own, key);
        }

        void applySets(App &app, const Json &sets)
        {
            std::vector<std::pair<std::string, int>> values;
            for (size_t i = 0; i < sets.size(); ++i)
                values.push_back({sets[i][0].str(), int(sets[i][1].num())});
            app.gameSettings.setMany(values);
            const int language = app.gameSettings.value("ui.language");
            if (language != app.language)
                app.setLanguage(language);
        }

        // ---- the controls (each in r, the row's right part) -------------------------------------------------------

        void keyButton(App &app, const Json &s, Rect r, bool enabled)
        {
            const std::string key = s["key"].str();
            const bool capturing = S.capturing == key;
            const int v = app.gameSettings.value(key);
            ui::pushId(key);
            const ui::Interaction in = ui::interact(ui::id("#bind"), r, true, enabled);
            const Color edge = capturing ? theme::kPink : theme::ink(0.08f + 0.06f * in.w->hover);
            rectEx(r, theme::kSurfaceHi, theme::kSurface, px(7), edge, capturing ? px(1.5f) : ui::hairline());
            const std::string shown = capturing ? core::i18n::tr("Press a key...") : app.gameSettings.keyName(v);
            ui::label(kFontSemibold, px(theme::kSmall), r.inset(px(8), 0), capturing ? theme::kPink : v ? theme::kText : theme::kFaint,
                      shown, ui::Align::Centre);
            if (in.clicked && !capturing)
                S.capturing = key;
            if (in.rightClicked && s["optional"].boolean())
            {
                app.gameSettings.set(key, 0);
                S.capturing.clear();
            }
            if (capturing && !in.clicked)
            {
                const int k = capturedKey();
                if (k == -2)
                    S.capturing.clear();
                else if (k > 0)
                {
                    bind(app, s, k);
                    S.capturing.clear();
                }
            }
            ui::popId();
        }

        void stepper(App &app, const Json &s, Rect r, bool enabled)
        {
            const std::string key = s["key"].str();
            const Json &choices = s["choices"];
            const int n = int(choices.size());
            const int v = app.gameSettings.value(key);
            int at = 0;
            for (int i = 0; i < n; ++i)
                if (int(choices[size_t(i)]["value"].num()) == v)
                    at = i;
            ui::pushId(key);
            rectEx(r, theme::kBase, theme::kBase, px(theme::kRadiusSmall + 1), theme::ink(0.06f), ui::hairline());
            const float bw = r.h;
            for (int side = 0; side < 2; ++side)
            {
                const Rect b{side ? r.right() - bw : r.x, r.y, bw, r.h};
                const bool can = enabled && (side ? at + 1 < n : at > 0);
                const ui::Interaction in = ui::interact(ui::id(side ? "#next" : "#prev"), b, true, can);
                if (in.w->hover > 0.01f)
                    rect(b.inset(px(3)), theme::ink(0.06f * in.w->hover), px(theme::kRadiusSmall));
                const float cx = b.cx(), cy = b.cy(), a = px(4.5f);
                const Color c = can ? mix(theme::kDim, theme::kText, in.w->hover) : theme::ink(0.15f);
                const float dir = side ? 1.0f : -1.0f;
                line(cx - dir * a * 0.5f, cy - a, cx + dir * a * 0.5f, cy, px(1.6f), c);
                line(cx + dir * a * 0.5f, cy, cx - dir * a * 0.5f, cy + a, px(1.6f), c);
                if (in.clicked && can)
                    app.gameSettings.set(key, int(choices[size_t(at + (side ? 1 : -1))]["value"].num()));
            }
            ui::label(kFontSemibold, px(theme::kSmall), {r.x + bw, r.y, r.w - 2 * bw, r.h}, theme::kText,
                      GameSettings::text(choices[size_t(at)]["label"]), ui::Align::Centre);
            ui::popId();
        }

        void slider(App &app, const Json &s, Rect r, bool enabled)
        {
            const std::string key = s["key"].str();
            const int lo = int(s["min"].num()), hi = int(s["max"].num());
            const int v = app.gameSettings.value(key);
            const float valueW = px(64);
            const Rect track{r.x + px(8), r.cy() - px(2), r.w - valueW - px(16), px(4)};
            ui::pushId(key);
            const ui::Interaction in = ui::interact(ui::id("#slider"), {track.x - px(8), r.y, track.w + px(16), r.h}, true, enabled);
            if (in.held && hi > lo)
            {
                const float t = core::clamp01((platform::input.mouseX - track.x) / track.w);
                const int nv = lo + int(t * float(hi - lo) + 0.5f);
                if (nv != v)
                    app.gameSettings.set(key, nv);
            }
            const float t = hi > lo ? float(v - lo) / float(hi - lo) : 0;
            rect(track, theme::ink(0.10f), px(2));
            gradient({track.x, track.y, track.w * t, track.h}, theme::kPink, theme::kPinkHi, px(2), Gradient::Horizontal);
            const float kx = track.x + track.w * t, ky = track.cy();
            circle(kx, ky, px(8 + 1.5f * in.w->hover), theme::kText);
            circle(kx, ky, px(4), theme::kPink);
            if (in.w->focus > 0.01f)
                outline({kx - px(12), ky - px(12), px(24), px(24)}, withAlpha(theme::kText, in.w->focus), px(2), px(12));
            ui::label(kFontSemibold, px(theme::kSmall), {r.right() - valueW, r.y, valueW, r.h}, theme::kText, formatted(s, v), ui::Align::Right);
            ui::popId();
        }

        void actionButton(App &app, const Json &s, Rect r, bool enabled)
        {
            const std::string key = s["key"].str();
            const bool confirming = S.confirming == key && ui::state.wall < S.confirmUntil;
            const std::string label = confirming ? core::i18n::tr("Click again to confirm")
                                                 : GameSettings::text(s.has("button") ? s["button"] : s["label"]);
            const float w = std::min(r.w, ui::buttonWidth(label + "##" + key));
            if (ui::button(label + "##" + key, {r.right() - w, r.y, w, r.h}, confirming ? ButtonStyle::Danger : ButtonStyle::Secondary, 0, enabled))
            {
                if (s["confirm"].boolean() && !confirming)
                {
                    S.confirming = key;
                    S.confirmUntil = ui::state.wall + 3;
                    ui::wakeAt(S.confirmUntil);
                }
                else
                {
                    S.confirming.clear();
                    applySets(app, s["sets"]);
                    toast(core::i18n::tr("Done: the game starts with these settings."));
                }
            }
        }

        bool fits(const Json &choices, float width)
        {
            const size_t n = choices.size();
            if (n < 2 || n > 3)
                return false;
            for (size_t i = 0; i < n; ++i)
                if (measure(kFontSemibold, px(theme::kSmall), GameSettings::text(choices[i]["label"])) > width / float(n) - px(18))
                    return false;
            return true;
        }

        // the page tabs, wrapped; returns the y under them
        float tabs(const std::vector<std::pair<std::string, std::string>> &pages, Rect r)
        {
            float x = r.x, y = r.y;
            const float h = px(32);
            for (const auto &[english, shown] : pages)
            {
                const float w = measure(kFontSemibold, px(theme::kSmall), shown) + px(26);
                if (x + w > r.right() && x > r.x)
                {
                    x = r.x;
                    y += h + px(6);
                }
                const Rect t{x, y, w, h};
                const ui::Interaction in = ui::interact(ui::id("#tab", int(std::hash<std::string>{}(english) & 0x7fffffff)), t);
                const bool on = S.page == english;
                if (on)
                    rect(t, theme::kPink, px(h * 0.5f));
                else
                    rect(t, theme::ink(0.04f + 0.05f * in.w->hover), px(h * 0.5f));
                ui::label(kFontSemibold, px(theme::kSmall), t, on ? theme::kOnPink : mix(theme::kDim, theme::kText, in.w->hover), shown, ui::Align::Centre);
                if (in.w->focus > 0.01f)
                    outline(t.inset(-px(3)), withAlpha(theme::kText, in.w->focus), px(2), px(h * 0.5f + 3));
                if (in.clicked)
                {
                    S.page = english;
                    S.capturing.clear();
                }
                x += w + px(6);
            }
            return y + h + px(18);
        }
    }

    bool gameSettingsCapturing() { return !S.capturing.empty(); }

    std::string gameSettingsHelp() { return S.help; }

    void gameSettingsPage(const std::string &english) { S.page = english; }

    float drawGameSettings(App &app, Rect r)
    {
        GameSettings &g = app.gameSettings;
        float y = r.y;
        if (!g.ready())
        {
            if (g.describing() || g.error().empty())
            {
                ui::spinner(r.x + px(9), y + px(11), px(7), theme::kDim, px(1.8f));
                ui::label(kFontBody, px(theme::kBody), {r.x + px(26), y, r.w, px(22)}, theme::kDim, "Loading...");
                return y + px(34);
            }
            y += ui::paragraph(kFontBody, px(theme::kBody), {r.x, y, r.w, 0}, theme::kDim,
                               g.error() == "not built" ? "Build the game first: these are its own settings, and the game describes them."
                                                        : "The game didn't describe its settings: open the game's log, or build again.");
            return y + px(8);
        }
        const bool enabled = !app.gameRunning && !app.busy();
        if (app.gameRunning)
        {
            y += ui::paragraph(kFontBody, px(theme::kBody), {r.x, y, r.w, 0}, theme::kAmber,
                               "The game is running: aspect ratio, HUD, frame rate cap and volume change in its pause menu (Display, Audio), the rest here once it is closed.") + px(12);
            S.capturing.clear();
        }

        // pages in the game's order
        std::vector<std::pair<std::string, std::string>> pages;
        const Json &all = g.settings();
        for (size_t i = 0; i < all.size(); ++i)
        {
            const std::string p = all[i]["page"]["en"].str();
            if (std::none_of(pages.begin(), pages.end(), [&](const auto &e) { return e.first == p; }))
                pages.push_back({p, GameSettings::text(all[i]["page"])});
        }
        if (std::none_of(pages.begin(), pages.end(), [&](const auto &e) { return e.first == S.page; }))
            S.page = pages.empty() ? "" : pages[0].first;
        y = tabs(pages, {r.x, y, r.w, 0});

        const float rowH = px(40), labelW = r.w * 0.46f;
        const Rect controls{r.x + r.w * 0.5f, 0, r.w * 0.5f, 0};
        std::string section;
        bool firstSection = true;
        for (size_t i = 0; i < all.size(); ++i)
        {
            const Json &s = all[i];
            if (s["page"]["en"].str() != S.page || s["hidden"].boolean())
                continue;
            if (s["section"]["en"].str() != section)
            {
                section = s["section"]["en"].str();
                y += firstSection ? 0 : px(14);
                firstSection = false;
                ui::eyebrow(r.x, y, theme::kFaint, upper(GameSettings::text(s["section"])));
                y += px(26);
            }
            const std::string type = s["type"].str();
            const Rect rowR{r.x - px(10), y, r.w + px(20), rowH};
            const bool over = ui::mouseOver(rowR);
            if (over)
            {
                rect(rowR, theme::ink(0.035f), px(8));
                S.help = GameSettings::text(s["help"]);
            }
            ui::label(kFontBody, px(theme::kBody), {r.x, y, labelW, rowH}, theme::kText, GameSettings::text(s["label"]));
            const Rect c{controls.x, y + px(4), controls.w, rowH - px(8)};
            if (type == "toggle")
            {
                bool on = g.value(s["key"].str()) != 0;
                if (ui::toggle("#t" + s["key"].str(), {c.right() - px(44), y + px(5), px(44), rowH - px(10)}, on, enabled))
                    g.set(s["key"].str(), on ? 1 : 0);
            }
            else if (type == "choice")
            {
                const Json &choices = s["choices"];
                if (fits(choices, c.w))
                {
                    std::vector<std::string> labels;
                    int selected = 0;
                    for (size_t k = 0; k < choices.size(); ++k)
                    {
                        labels.push_back(GameSettings::text(choices[k]["label"]) + "##" + std::to_string(k));
                        if (int(choices[k]["value"].num()) == g.value(s["key"].str()))
                            selected = int(k);
                    }
                    if (ui::segmented("#c" + s["key"].str(), c, labels, selected, enabled))
                    {
                        g.set(s["key"].str(), int(choices[size_t(selected)]["value"].num()));
                        if (s["key"].str() == "ui.language")
                            app.setLanguage(g.value("ui.language"));
                    }
                }
                else
                {
                    const int before = g.value(s["key"].str());
                    stepper(app, s, c, enabled);
                    if (s["key"].str() == "ui.language" && g.value("ui.language") != before)
                        app.setLanguage(g.value("ui.language"));
                }
            }
            else if (type == "integer")
                slider(app, s, c, enabled);
            else if (type == "key")
            {
                const Json *partner = s["partner"].str().empty() ? nullptr : g.find(s["partner"].str());
                if (partner)
                {
                    const float half = (c.w - px(8)) * 0.5f;
                    keyButton(app, s, {c.x, c.y, half, c.h}, enabled);
                    keyButton(app, *partner, {c.x + half + px(8), c.y, half, c.h}, enabled);
                }
                else
                    keyButton(app, s, c, enabled);
            }
            else if (type == "action")
                actionButton(app, s, c, enabled);
            y += rowH + px(2);
        }

        // the page back to the game's defaults
        y += px(14);
        rect({r.x, y, r.w, 1}, theme::ink(0.06f));
        y += px(16);
        if (ui::button("Restore this page's defaults", {r.x, y, ui::buttonWidth("Restore this page's defaults", theme::icon::kUndo), px(38)},
                       ButtonStyle::Ghost, theme::icon::kUndo, enabled))
        {
            std::vector<std::pair<std::string, int>> values;
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i]["page"]["en"].str() == S.page && all[i]["type"].str() != "action")
                    values.push_back({all[i]["key"].str(), int(all[i]["default"].num())});
            g.setMany(values);
            if (g.value("ui.language") != app.language)
                app.setLanguage(g.value("ui.language"));
        }
        return y + px(48);
    }
}
