// Debug aids for hangs:
// - VCS_WATCHDOG=<seconds>: prints the EE thread states (pc/ra/status/wait) periodically so hangs show where every
//   thread is.
// - The stall report (on unless VCS_STALL_REPORT=0): when the game ends no frame for 6 s while VBlanks go on (the
//   picture stuck, the window and its menus alive - a player's report, 2026-11-19), the EE / IOP threads, semaphores
//   and event flags (four snapshots a second apart) and the F3 game-state lines go to <data>/logs/stall-<time>.txt and
//   a line to the log. Written even when the game was started without the launcher (no log kept then).
#pragma once

#include <filesystem>

class PS2Runtime;

namespace vcs
{
    void startWatchdog(PS2Runtime &runtime);
    void startStallReport(PS2Runtime &runtime, const std::filesystem::path &folder);
}
