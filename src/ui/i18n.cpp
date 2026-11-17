// See i18n.h.
#include "ui/i18n.h"

#include "settings/settings.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>
#include <unordered_map>
#include <utility>

namespace ui::i18n
{
    namespace
    {
        // English -> Spanish, every text the menus show (generated pairs: i18n_es.inc)
        const std::pair<const char *, const char *> kSpanish[] = {
#include "ui/i18n_es.inc"
        };

        std::atomic<bool> g_spanish{true};

        const std::unordered_map<std::string, std::string> &table()
        {
            static const std::unordered_map<std::string, std::string> t = [] {
                std::unordered_map<std::string, std::string> m;
                for (const auto &[en, es] : kSpanish)
                    m.emplace(en, es);
                return m;
            }();
            return t;
        }
    }

    void registerSetting()
    {
        settings::Spec s;
        s.key = "ui.language";
        s.page = "Game";
        s.section = "Language";
        s.label = "Language / Idioma";
        s.help = "The language of these menus (F2, F5). The game's own text is the disc's.";
        s.type = settings::Type::Choice;
        s.choices = {{"English", 0}, {"Español", 1}};
        s.defaultValue = 1;
        s.env = "LCS_LANGUAGE";
        s.onChange = [](int v) { g_spanish.store(v == 1, std::memory_order_relaxed); };
        settings::add(s);
    }

    bool spanish() { return g_spanish.load(std::memory_order_relaxed); }

    const std::string &tr(const std::string &english)
    {
        // the texts a caller passes are kept, so a reference to one stays valid (menus pass temporaries too)
        static std::mutex mutex;
        static std::set<std::string> kept;
        if (spanish())
        {
            const auto &t = table();
            if (const auto it = t.find(english); it != t.end())
                return it->second;
        }
        std::lock_guard<std::mutex> lock(mutex);
        return *kept.insert(english).first;
    }

    void dumpMissing()
    {
        const char *path = std::getenv("LCS_I18N_DUMP");
        if (!path || !*path)
            return;
        std::set<std::string> texts;
        for (const settings::Spec &s : settings::all())
        {
            for (const std::string *t : {&s.page, &s.section, &s.label, &s.help, &s.button, &s.lockedNote})
                if (!t->empty())
                    texts.insert(*t);
            for (const settings::Choice &c : s.choices)
                texts.insert(c.label);
        }
        FILE *f = std::fopen(path, "wb");
        if (!f)
            return;
        int n = 0;
        for (const std::string &t : texts)
            if (!table().count(t))
            {
                std::fprintf(f, "%s\n", t.c_str());
                ++n;
            }
        std::fclose(f);
        std::fprintf(stderr, "[i18n] %d texts without Spanish written to %s\n", n, path);
    }
}
