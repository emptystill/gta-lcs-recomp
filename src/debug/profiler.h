// Debug aid: sampling profiler for the recompiled game.
//
// VCS_PROFILE=<seconds between reports> samples the "GameThread" host thread's
// instruction pointer ~2000x per second and maps it back to the guest function
// whose recompiled body contains it (nearest lower host function pointer in the
// generated function table). Prints the hottest guest functions every report.
// Works without debug symbols. Windows (SuspendThread) and Linux (a SIGPROF per sample).
#pragma once

#include <string>

class PS2Runtime;

namespace vcs
{
    void startProfiler(PS2Runtime &runtime);
    // VCS_PROFILE_SPIKES=<ms> (with VCS_PROFILE): the game thread calls this once a frame (at the frame kick); a frame
    // longer than <ms> gets its own report - the samples taken during it, by guest function / host symbol ("[spike]")
    void profilerFrame();
    // Any thread: every host thread's stack, by name, symbolised (Windows; empty elsewhere). Each thread is suspended
    // only while its context is read and unwound. The stall report (debug/watchdog.cpp) uses it.
    std::string hostThreadStacks();
}
