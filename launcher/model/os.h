// What the launcher calls things on each OS, in one place: the binaries, the file manager, folder separators in text.
#pragma once

namespace launcher::os
{
#if defined(__APPLE__)
    constexpr bool kMac = true;
#else
    constexpr bool kMac = false;
#endif
#ifdef _WIN32
    constexpr bool kWindows = true;
    constexpr const char *kGameExe = "lcs.exe";
    constexpr const char *kLauncherExe = "LCSLauncher.exe";
    constexpr const char *kFileManager = "Explorer";
    constexpr const char *kShortcutName = "GTA Liberty City Stories.lnk";
    constexpr const char *kBuildGame = "build\\lcs.exe"; // as the player sees it in messages
    constexpr const char *kBuildIni = "build\\lcs.ini";
    constexpr const char *kTools = "tools\\";
#elif defined(__APPLE__)
    constexpr bool kWindows = false;
    constexpr const char *kGameExe = "lcs";
    constexpr const char *kLauncherExe = "LCSLauncher.app";
    constexpr const char *kFileManager = "Finder";
    constexpr const char *kShortcutName = "GTA Liberty City Stories";
    constexpr const char *kBuildGame = "build/lcs";
    constexpr const char *kBuildIni = "build/lcs.ini";
    constexpr const char *kTools = "tools/";
#else
    constexpr bool kWindows = false;
    constexpr const char *kGameExe = "lcs";
    constexpr const char *kLauncherExe = "LCSLauncher";
    constexpr const char *kFileManager = "Files";
    constexpr const char *kShortcutName = "gta-vcs-launcher.desktop";
    constexpr const char *kBuildGame = "build/lcs";
    constexpr const char *kBuildIni = "build/lcs.ini";
    constexpr const char *kTools = "tools/";
#endif
}
