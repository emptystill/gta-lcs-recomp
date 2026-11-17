// See i18n.h.
#include "core/i18n.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core::i18n
{
    namespace
    {
        const std::pair<const char *, const char *> kSpanish[] = {
#include "core/i18n_es.inc"
        };

        std::atomic<bool> g_spanish{false};

        struct Table
        {
            std::unordered_map<std::string, std::string> whole;
            // patterns: the English split at its holes, and the Spanish with its holes
            struct Pattern
            {
                std::vector<std::string> parts; // n + 1 pieces around n holes
                std::string spanish;
            };
            std::vector<Pattern> patterns;
        };

        std::vector<std::string> split(const std::string &s)
        {
            std::vector<std::string> parts;
            size_t at = 0;
            for (size_t hole; (hole = s.find("{}", at)) != std::string::npos; at = hole + 2)
                parts.push_back(s.substr(at, hole - at));
            parts.push_back(s.substr(at));
            return parts;
        }

        const Table &table()
        {
            static const Table t = [] {
                Table m;
                for (const auto &[en, es] : kSpanish)
                {
                    const std::string e = en;
                    if (e.find("{}") != std::string::npos)
                        m.patterns.push_back({split(e), es});
                    else
                        m.whole.emplace(e, es);
                }
                return m;
            }();
            return t;
        }

        // the holes' texts when `text` is the pattern (pieces in order, each hole at least one character)
        bool match(const std::vector<std::string> &parts, std::string_view text, std::vector<std::string> &holes)
        {
            if (text.substr(0, parts[0].size()) != parts[0])
                return false;
            size_t at = parts[0].size();
            holes.clear();
            for (size_t k = 1; k < parts.size(); ++k)
            {
                const std::string &piece = parts[k];
                size_t found;
                if (k + 1 == parts.size()) // the last piece ends the text
                {
                    if (text.size() < at + 1 + piece.size() || text.substr(text.size() - piece.size()) != piece)
                        return false;
                    found = text.size() - piece.size();
                }
                else
                {
                    found = piece.empty() ? std::string_view::npos : text.find(piece, at + 1);
                    if (found == std::string_view::npos)
                        return false;
                }
                holes.emplace_back(text.substr(at, found - at));
                at = found + piece.size();
            }
            return true;
        }

        const char *dumpPath()
        {
            static const char *path = std::getenv("LCS_I18N_DUMP");
            return path && *path ? path : nullptr;
        }

        std::mutex g_dumpMutex;
        std::vector<std::string> g_given; // the Spanish handed out, whose lines come back to be drawn

        void given(const std::string &spanish)
        {
            if (!dumpPath())
                return;
            std::lock_guard<std::mutex> lock(g_dumpMutex);
            if (std::find(g_given.begin(), g_given.end(), spanish) == g_given.end())
                g_given.push_back(spanish);
        }

        void dump(std::string_view text)
        {
            const char *path = dumpPath();
            if (!path)
                return;
            static std::set<std::string> seen;
            std::lock_guard<std::mutex> lock(g_dumpMutex);
            if (!seen.insert(std::string(text)).second)
                return;
            // a wrapped or shortened line of a text already in Spanish
            std::string_view whole = text;
            if (whole.size() >= 3 && whole.substr(whole.size() - 3) == "\xE2\x80\xA6")
                whole.remove_suffix(3);
            for (const std::string &g : g_given)
                if (g.find(whole) != std::string::npos)
                    return;
            if (FILE *f = std::fopen(path, "ab"))
            {
                std::fprintf(f, "%.*s\n", int(text.size()), text.data());
                std::fclose(f);
            }
        }

        bool hasLetters(std::string_view t)
        {
            int letters = 0;
            for (char c : t)
                letters += (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
            return letters >= 2;
        }
    }

    void setSpanish(bool on) { g_spanish.store(on, std::memory_order_relaxed); }
    bool spanish() { return g_spanish.load(std::memory_order_relaxed); }

    std::string tr(std::string_view english)
    {
        if (!spanish() || english.empty())
            return std::string(english);
        const Table &t = table();
        if (const auto it = t.whole.find(std::string(english)); it != t.whole.end())
        {
            given(it->second);
            return it->second;
        }
        std::vector<std::string> holes;
        for (const Table::Pattern &p : t.patterns)
            if (match(p.parts, english, holes))
            {
                std::string out;
                size_t at = 0, k = 0;
                for (size_t hole; (hole = p.spanish.find("{}", at)) != std::string::npos; at = hole + 2)
                    out += p.spanish.substr(at, hole - at) + (k < holes.size() ? tr(holes[k++]) : std::string());
                out += p.spanish.substr(at);
                given(out);
                return out;
            }
        if (hasLetters(english))
            dump(english);
        return std::string(english);
    }
}
