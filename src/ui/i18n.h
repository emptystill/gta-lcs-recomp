// The menus' language (Game > Language / Idioma): English, or Spanish - the user's game is Spanish (DECISIONS.md
// 2026-11-17). Every text the settings menu (F2) and the trainer (F5) show goes through tr(): the English text is the
// key (the settings registry and the menus are written in English), the Spanish one comes from i18n_es.inc. A text the
// table lacks shows in English. LCS_I18N_DUMP=<file> writes the registry's texts the table lacks (a translation pass).
#pragma once

#include <string>

namespace ui::i18n
{
    // The Game page's Language row (main.cpp, with the other settings).
    void registerSetting();
    bool spanish();
    // The text in the menus' language; the reference stays valid (texts are kept).
    const std::string &tr(const std::string &english);
    inline const char *trc(const char *english) { return tr(english).c_str(); }
    // Once, after every setting is registered: LCS_I18N_DUMP.
    void dumpMissing();
}
