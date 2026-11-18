// See game_settings.h.
#include "model/game_settings.h"

#include "core/i18n.h"
#include "model/os.h"
#include "model/system.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace launcher
{
    namespace fs = std::filesystem;

    namespace
    {
        std::string readAll(const fs::path &path)
        {
            std::ifstream in(path, std::ios::binary);
            std::ostringstream s;
            s << in.rdbuf();
            return s.str();
        }

        std::string trimmed(std::string s)
        {
            while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
                s.pop_back();
            size_t i = 0;
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
                ++i;
            return s.substr(i);
        }
    }

    void GameSettings::refresh(const fs::path &build)
    {
        m_build = build;
        m_ini = build / "lcs.ini";
        m_cache = build / "settings_schema.json";
        reloadValues();
        if (m_process)
            return;
        const fs::path exe = build / os::kGameExe;
        std::error_code ec;
        if (!fs::is_regular_file(exe, ec))
        {
            m_schema = Json();
            m_error = "not built";
            return;
        }
        const bool fresh = fs::is_regular_file(m_cache, ec) && fs::last_write_time(m_cache, ec) >= fs::last_write_time(exe, ec);
        if (fresh && !ready())
            loadSchema();
        if (fresh && ready())
            return;
        // lcs.exe describes its settings and ends (no window, nothing loaded)
        Process::Options options;
        options.cwd = build;
        m_process = std::make_unique<Process>();
        std::string error;
        if (!m_process->start({pathText(exe), "--settings-json", pathText(m_cache)}, options, error))
        {
            m_process.reset();
            m_error = error;
        }
    }

    void GameSettings::update()
    {
        if (!m_process)
            return;
        m_process->takeLines();
        if (m_process->running())
            return;
        const int code = m_process->exitCode();
        m_process.reset();
        if (code != 0)
            m_error = "lcs.exe --settings-json: exit code " + std::to_string(code);
        loadSchema();
    }

    void GameSettings::loadSchema()
    {
        bool ok = false;
        Json schema = Json::parse(readAll(m_cache), &ok);
        if (!ok || !schema.isObject() || schema["settings"].size() == 0)
        {
            if (m_error.empty())
                m_error = "no settings";
            return;
        }
        m_schema = schema;
        m_error.clear();
        m_keyNames.clear();
        const Json &keys = m_schema["keys"];
        for (size_t i = 0; i < keys.size(); ++i)
            m_keyNames[int(keys[i]["code"].num())] = {keys[i]["name"]["en"].str(), keys[i]["name"]["es"].str()};
        reloadValues();
    }

    const Json *GameSettings::find(const std::string &key) const
    {
        const Json &all = settings();
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i]["key"].str() == key)
                return &all[i];
        return nullptr;
    }

    std::string GameSettings::text(const Json &texts)
    {
        const std::string es = texts["es"].str();
        return core::i18n::spanish() && !es.empty() ? es : texts["en"].str();
    }

    std::string GameSettings::keyName(int code) const
    {
        if (code == 0)
            return "-";
        const auto it = m_keyNames.find(code);
        if (it == m_keyNames.end())
            return "?";
        return core::i18n::spanish() && !it->second.second.empty() ? it->second.second : it->second.first;
    }

    int GameSettings::value(const std::string &key) const
    {
        if (const auto it = m_values.find(key); it != m_values.end())
            return it->second;
        const Json *s = find(key);
        return s ? int((*s)["default"].num()) : 0;
    }

    bool GameSettings::valid(const Json &s, int v) const
    {
        const std::string type = s["type"].str();
        if (type == "toggle")
            return v == 0 || v == 1;
        if (type == "integer")
            return v >= int(s["min"].num()) && v <= int(s["max"].num());
        if (type == "choice")
        {
            for (size_t i = 0; i < s["choices"].size(); ++i)
                if (int(s["choices"][i]["value"].num()) == v)
                    return true;
            return false;
        }
        if (type == "key")
            return (v > 0 || (v == 0 && s["optional"].boolean())) && v < 512;
        return false;
    }

    void GameSettings::reloadValues()
    {
        m_values.clear();
        std::map<std::string, std::string> found;
        {
            std::ifstream in(m_ini);
            std::string line;
            while (std::getline(in, line))
            {
                const std::string t = trimmed(line);
                const size_t eq = t.find('=');
                if (t.empty() || t[0] == '#' || t[0] == ';' || eq == std::string::npos)
                    continue;
                found[trimmed(t.substr(0, eq))] = trimmed(t.substr(eq + 1));
            }
        }
        // as the game reads it (settings::load): the key, else its older alias; values it would refuse are ignored
        const Json &all = settings();
        for (size_t i = 0; i < all.size(); ++i)
        {
            const Json &s = all[i];
            auto it = found.find(s["key"].str());
            if (it == found.end() && !s["alias"].str().empty())
                it = found.find(s["alias"].str());
            if (it == found.end())
                continue;
            const int v = std::atoi(it->second.c_str());
            if (valid(s, v))
                m_values[s["key"].str()] = v;
        }
    }

    bool GameSettings::set(const std::string &key, int v)
    {
        const Json *s = find(key);
        if (!s || !valid(*s, v))
            return false;
        m_values[key] = v;
        write();
        return true;
    }

    void GameSettings::setMany(const std::vector<std::pair<std::string, int>> &values)
    {
        for (const auto &[key, v] : values)
            if (const Json *s = find(key); s && valid(*s, v))
                m_values[key] = v;
        write();
    }

    // lcs.ini rewritten as the game does it: its keys updated in place, new ones appended, every other line kept
    void GameSettings::write()
    {
        std::vector<std::string> lines;
        {
            std::ifstream in(m_ini);
            std::string line;
            while (std::getline(in, line))
                lines.push_back(line.empty() || line.back() != '\r' ? line : line.substr(0, line.size() - 1));
        }
        std::map<std::string, bool> written;
        for (std::string &line : lines)
        {
            const std::string t = trimmed(line);
            const size_t eq = t.find('=');
            if (t.empty() || t[0] == '#' || t[0] == ';' || eq == std::string::npos)
                continue;
            const std::string key = trimmed(t.substr(0, eq));
            if (const auto it = m_values.find(key); it != m_values.end())
            {
                line = key + "=" + std::to_string(it->second);
                written[key] = true;
            }
        }
        for (const auto &[key, v] : m_values)
            if (!written[key])
                lines.push_back(key + "=" + std::to_string(v));
        std::error_code ec;
        fs::create_directories(m_build, ec);
        fs::path tmp = m_ini;
        tmp += ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            for (const std::string &l : lines)
                out << l << "\n";
            if (!out)
                return;
        }
        fs::rename(tmp, m_ini, ec);
    }
}
