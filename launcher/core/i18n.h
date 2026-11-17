// The launcher's language: English, or Spanish (the game's menus' choice, lcs.ini's ui.language; DECISIONS.md
// 2026-11-17). The UI is written in English; render/font.cpp passes every text it measures or draws through tr(), so a
// screen needs no change - the Spanish comes from core/i18n_es.inc: whole texts, and patterns with {} holes for the
// texts with numbers or names in them ("{} of {} steps ready"), whose holes are translated in turn.
// LCS_I18N_DUMP=<file>: every text drawn that has no Spanish is appended to the file (a translation pass).
#pragma once

#include <string>
#include <string_view>

namespace core::i18n
{
    void setSpanish(bool on);
    bool spanish();
    // the text in the launcher's language (the same text when there is none, or in English)
    std::string tr(std::string_view english);
}
