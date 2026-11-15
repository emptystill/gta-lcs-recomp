// The one supported disc: what identifies it, shared by vcs.exe (src/main.cpp) and the launcher (launcher/), so the
// two can't disagree. tools/vcs_disc.py holds the same values for the build (KNOWN_BUILDS).
#pragma once

#include <string>
#include <string_view>

namespace vcs::disc
{
    constexpr const char *kElfName = "SLUS_214.23";
    constexpr const char *kElfSha1 = "9b021b4c4e42087bb9b374709f26cf67744b75ab";
    constexpr const char *kTitle = "GTA: Liberty City Stories - USA (SLUS-21423) v1.01";
    constexpr const char *kGameName = "GTA: Liberty City Stories"; // the window's title
    constexpr const char *kSupported = "GTA: Liberty City Stories for the USA (SLUS-21423)";

    // vcs.exe's exit codes for a start that never reached the game (the launcher names the reason)
    enum ExitCode : int
    {
        kExitOk = 0,
        kExitError = 1,       // anything else (an exception, the runtime stopping)
        kExitNoDisc = 2,      // no disc image given or found
        kExitDiscUnreadable = 3,
        kExitWrongDisc = 4,   // not SLUS_214.23, or another build of it
        kExitInitFailed = 5,  // the runtime (window, renderer, sound) could not start
    };

    // A disc's boot executable named by SYSTEM.CNF ("BOOT2 = cdrom0:\SLES_546.22;1" -> "SLES_546.22"), empty when
    // there is none (not a PS2 disc).
    inline std::string bootExecutable(std::string_view systemCnf)
    {
        const size_t boot = systemCnf.find("BOOT2");
        if (boot == std::string_view::npos)
            return {};
        size_t start = systemCnf.find_first_of("\\:", boot);
        while (start != std::string_view::npos && start + 1 < systemCnf.size() &&
               (systemCnf[start] == '\\' || systemCnf[start] == ':'))
            ++start;
        if (start == std::string_view::npos)
            return {};
        size_t end = start;
        while (end < systemCnf.size() && systemCnf[end] != ';' && systemCnf[end] != '\r' && systemCnf[end] != '\n' &&
               systemCnf[end] != ' ')
            ++end;
        return std::string(systemCnf.substr(start, end - start));
    }

    // What a boot executable other than ours is, in words a player can act on (empty when unknown).
    inline const char *describeOtherRelease(std::string_view elf)
    {
        if (elf == "SLUS_215.90")
            return "Vice City Stories (USA), not Liberty City Stories (the VCS port is its own project)";
        if (elf == "SLES_546.22")
            return "the European release of Vice City Stories";
        if (elf == "SLES_541.35")
            return "the European release of Liberty City Stories";
        return "";
    }
}
