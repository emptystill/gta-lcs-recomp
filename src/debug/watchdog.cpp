#include "watchdog.h"

#include "game_info.h"
#include "profiler.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_perf.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>

namespace vcs
{
    void startWatchdog(PS2Runtime &runtime)
    {
        const char *env = std::getenv("VCS_WATCHDOG");
        if (!env || !*env)
            return;
        const double seconds = std::atof(env) > 0.0 ? std::atof(env) : 1.0;

        std::thread([&runtime, seconds]()
                    {
            const auto period = std::chrono::milliseconds(static_cast<int>(seconds * 1000.0));
            uint64_t lastCycle = 0;
            while (!runtime.isStopRequested())
            {
                std::this_thread::sleep_for(period);
                const EeKernelSnapshot snap = runtime.eeScheduler().snapshot();
                std::fprintf(stderr, "[watchdog] cycle=%llu (+%llu) running=%d pc=%08x ra=%08x\n",
                             static_cast<unsigned long long>(snap.eeCycle),
                             static_cast<unsigned long long>(snap.eeCycle - lastCycle),
                             snap.runningThreadId,
                             runtime.m_debugPc.load(std::memory_order_relaxed),
                             runtime.m_debugRa.load(std::memory_order_relaxed));
                lastCycle = snap.eeCycle;
                const ps2x::iop::DebugSnapshot iop = runtime.iopDebugSnapshot();
                std::fprintf(stderr, "[watchdog] iop cycles=%llu instructions=%llu modules=%u threads=%u rpcServers=%u\n",
                             static_cast<unsigned long long>(iop.emulatorCycles),
                             static_cast<unsigned long long>(iop.emulatorInstructions),
                             iop.emulatorLoadedModules, iop.emulatorThreads, iop.emulatorRpcServers);
                for (const std::string &d : iop.diagnostics)
                    std::fprintf(stderr, "[watchdog] iop: %s\n", d.c_str());
                for (const EeThreadSnapshot &t : snap.threads)
                {
                    std::fprintf(stderr, "[watchdog]   thread %d prio=%d status=%d wait=%d/%d pc=%08x ra=%08x entry=%08x\n",
                                 t.id, t.currentPriority, static_cast<int>(t.status),
                                 static_cast<int>(t.waitReason), t.waitId, t.pc, t.ra, t.entry);
                }
                std::fflush(stderr);
            } })
            .detach();
    }

    namespace
    {
        std::string fmt(const char *format, ...)
        {
            char buffer[512];
            va_list args;
            va_start(args, format);
            std::vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            return buffer;
        }

        // one snapshot of the EE kernel and the IOP, as text
        std::string kernelState(PS2Runtime &runtime)
        {
            const EeKernelSnapshot s = runtime.eeScheduler().snapshot();
            std::string out = fmt("ee cycle=%llu next_event=%llu running=%d debug_pc=%08x debug_ra=%08x\n",
                                  static_cast<unsigned long long>(s.eeCycle),
                                  static_cast<unsigned long long>(s.nextEventCycle), s.runningThreadId,
                                  runtime.m_debugPc.load(std::memory_order_relaxed),
                                  runtime.m_debugRa.load(std::memory_order_relaxed));
            // status: 0 running 1 ready 2 waiting 3 waiting+suspended 4 suspended 5 dormant; wait: 1 sleep 2 sema
            // 3 event flag 4 vsync 5 external 6 mpeg
            for (const EeThreadSnapshot &t : s.threads)
                out += fmt("  thread %d prio=%d status=%d wait=%d/%d pc=%08x ra=%08x sp=%08x entry=%08x suspend=%d "
                           "wakeups=%u depth=%u\n",
                           t.id, t.currentPriority, static_cast<int>(t.status), static_cast<int>(t.waitReason), t.waitId,
                           t.pc, t.ra, t.sp, t.entry, t.suspendCount, t.wakeupCount, t.invocationDepth);
            for (const EeSemaphoreSnapshot &m : s.semaphores)
                out += fmt("  sema %d count=%d/%d waiters=%u\n", m.id, m.count, m.maxCount, m.waiters);
            for (const EeEventFlagSnapshot &e : s.eventFlags)
                out += fmt("  eventflag %d bits=%x waiters=%u\n", e.id, e.bits, e.waiters);
            const ps2x::iop::DebugSnapshot iop = runtime.iopDebugSnapshot();
            out += fmt("iop cycles=%llu instructions=%llu modules=%u threads=%u rpc_servers=%u\n",
                       static_cast<unsigned long long>(iop.emulatorCycles),
                       static_cast<unsigned long long>(iop.emulatorInstructions), iop.emulatorLoadedModules,
                       iop.emulatorThreads, iop.emulatorRpcServers);
            for (const std::string &d : iop.diagnostics)
                out += "  iop: " + d + "\n";
            return out;
        }
    }

    void startStallReport(PS2Runtime &runtime, const std::filesystem::path &folder)
    {
        if (const char *env = std::getenv("VCS_STALL_REPORT"); env && *env == '0')
            return;
        std::thread([&runtime, folder]()
                    {
            constexpr double kStallSeconds = 6.0;
            constexpr int kMaxReports = 3;
            int reports = 0;
            ps2perf::Liveness last = ps2perf::liveness();
            auto lastFrameAt = std::chrono::steady_clock::now();
            uint64_t vblanksAtLastFrame = last.vblanks;
            bool reported = false;
            while (!runtime.isStopRequested() && reports < kMaxReports)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                const ps2perf::Liveness now = ps2perf::liveness();
                const auto at = std::chrono::steady_clock::now();
                if (now.gameFrames != last.gameFrames)
                {
                    if (reported)
                        std::fprintf(stderr, "[stall] game frames again after the stall (frame %llu)\n",
                                     static_cast<unsigned long long>(now.gameFrames));
                    reported = false;
                    lastFrameAt = at;
                    vblanksAtLastFrame = now.vblanks;
                    last = now;
                    continue;
                }
                last.vblanks = now.vblanks;
                const double stalled = std::chrono::duration<double>(at - lastFrameAt).count();
                if (reported || now.gameFrames < 100 || stalled < kStallSeconds)
                    continue;
                reported = true;
                ++reports;

                const std::time_t wall = std::time(nullptr);
                char stamp[32] = "unknown";
                if (const std::tm *tm = std::localtime(&wall))
                    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", tm);
                // VBlanks going on: the EE runs and its threads wait (a semaphore, an event flag); none: the game
                // thread is held in host code (a lock, a wait for the renderer) - its host stack says where
                const uint64_t vblanks = now.vblanks - vblanksAtLastFrame;
                std::string text = fmt("stall report: no game frame for %.1f s; %llu VBlanks since the last frame (%s); "
                                       "game frames so far %llu\n",
                                       stalled, static_cast<unsigned long long>(vblanks),
                                       vblanks > 60 ? "the EE runs: a guest wait" : "the EE stands still: host code",
                                       static_cast<unsigned long long>(now.gameFrames));
                text += "\n== game state (F3 panel)\n" + vcs::gameinfo::text();
                for (int k = 0; k < 4; ++k)
                {
                    if (k)
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    text += fmt("\n== snapshot %d (+%d s), VBlanks since the frame %llu\n", k + 1, k,
                                static_cast<unsigned long long>(ps2perf::liveness().vblanks - vblanksAtLastFrame)) +
                            kernelState(runtime);
                    if (k == 0 || k == 3)
                        text += fmt("\n== host threads (+%d s)\n", k) + hostThreadStacks();
                }
                std::error_code ec;
                std::filesystem::create_directories(folder, ec);
                const std::filesystem::path file = folder / (std::string("stall-") + stamp + ".txt");
                std::ofstream(file, std::ios::binary) << text;
                std::fprintf(stderr, "[stall] no game frame for %.1f s: report %s\n%s", stalled,
                             file.string().c_str(), text.c_str());
                std::fflush(stderr);
            } })
            .detach();
    }
}
