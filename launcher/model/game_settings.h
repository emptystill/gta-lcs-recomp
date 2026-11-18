// The game's settings (its F2 menu) in the launcher: lcs.exe describes them (lcs.exe --settings-json, cached in
// build/settings_schema.json and made again whenever lcs.exe is newer), and their values live in build/lcs.ini next to
// lcs.exe - the file the game reads at start and writes when its menu changes something. The launcher only edits
// lcs.ini while the game is not running (the game would overwrite it). docs/LAUNCHER.md "Game settings".
#pragma once

#include "json.h"
#include "process.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace launcher
{
    class GameSettings
    {
    public:
        // where the build is (lcs.exe and lcs.ini); a new description is asked for when lcs.exe changed
        void refresh(const std::filesystem::path &build);
        void update(); // polls lcs.exe --settings-json
        bool describing() const { return m_process != nullptr; }
        bool ready() const { return m_schema.isObject(); }
        const std::string &error() const { return m_error; }

        const Json &settings() const { return m_schema["settings"]; }
        const Json *find(const std::string &key) const;
        // a text of the description in the launcher's language ({"en":..., "es":...})
        static std::string text(const Json &texts);
        std::string keyName(int code) const; // "-" for 0

        int value(const std::string &key) const;
        bool valid(const Json &setting, int v) const;
        // to lcs.ini (validated); several at once for an Action's sets
        bool set(const std::string &key, int v);
        void setMany(const std::vector<std::pair<std::string, int>> &values);
        void reloadValues(); // lcs.ini again (the game may have changed it)

    private:
        std::filesystem::path m_build, m_ini, m_cache;
        std::unique_ptr<Process> m_process;
        Json m_schema;
        std::string m_error;
        std::map<std::string, int> m_values;
        std::map<int, std::pair<std::string, std::string>> m_keyNames;
        void loadSchema();
        void write();
    };
}
