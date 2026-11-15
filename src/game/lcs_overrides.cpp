// Game-specific runtime bindings for Liberty City Stories (SLUS_214.23), applied when the ELF loads - the LCS
// counterpart of vcs_overrides.cpp (which only applies to VCS's executable). Each hook here is an LCS address with
// the VCS one it mirrors and how it was found (docs/LCS.md).
#include "game_overrides.h"
#include "game/vcs_stream.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#if defined(_WIN32)
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace
{
    PS2_GUEST_CC void returnZero(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        SET_GPR_U64(ctx, 2, 0);
        ctx->pc = GPR_U32(ctx, 31);
    }

    bool envOff(const char *name)
    {
        const char *v = std::getenv(name);
        return v && v[0] == '0';
    }

    bool envOn(const char *name)
    {
        const char *v = std::getenv(name);
        return v && v[0] && v[0] != '0';
    }

    // The movie player's Play(path, 0, n) (VCS's CMoviePlayer::Play 0x40D8B8). Found from the movie paths: the boot
    // function FUN_001F3728 (VCS FUN_0021CA70) loads cdrom0:\MOVIES\RSNY.PSS, RSLEEDS, RSNORTH and DM.PSS (0x3B5A58..)
    // and calls 0x359F40 (init, VCS 0x40D7D8), 0x35A020 for each (Play) and 0x35A000 (shut down, VCS 0x40D898).
    // Emulated, the first movie never finishes (the stream stops after a few reads, the game thread stays inside Play):
    // skipped until the movies play natively, as VCS did before game/vcs_movie.h. LCS_MOVIES=1 runs the game's own.
    constexpr uint32_t kMoviePlay = 0x0035A020u;

    // CDSTREAM.IRX's stream server, EE side (VCS's in game/vcs_stream.cpp): the same three functions, found by
    // tools/xmatch.py as exact matches of VCS's: read(channel, dest, lsn, size) 0x1F6CA0 (VCS 0x221818), poll(channel)
    // 0x1F6D78 (VCS 0x2218F0), fn 5 0x1F6E50 (VCS 0x221968). Through LCS's emulated IOP the reads come back wrong: the
    // text loader (0x3326A8) read all of TEXT\ENGLISH.GXT without finding its TKEY / TDAT sections and never left its
    // loop (the legal screen for ever). Natively the disc image is copied into EE RAM. VCS_NATIVE_STREAM=0: the IOP.
    constexpr uint32_t kStreamRead = 0x001F6CA0u;
    constexpr uint32_t kStreamPoll = 0x001F6D78u;
    constexpr uint32_t kStreamFn5 = 0x001F6E50u;
    // LCS's CDSTREAM has two more client calls than VCS's (decompiled, 2026-11-14): fn 3 wait(channel) 0x1F6EA0 (blocks
    // until the channel is idle; the model streamer 0x18F648 / 0x18F918 waits with it) and fn 4 0x1F6DE8 (no
    // arguments; the drive head's LSN - FUN_0018F018 picks the nearest copy of GTA3PS2.IMG by it).
    constexpr uint32_t kStreamWait = 0x001F6EA0u;
    constexpr uint32_t kStreamHead = 0x001F6DE8u;

    // ---- LCS_TRACE_ALLOC=1: the heap's large-block path and its out-of-memory handler, logged on entry ----
    // Heap: CMemoryHeap-like at 0x406D50 (FUN_0014F2B8), over 0x673168.. (+0x148AE98); FUN_00150478(heap, size) (VCS
    // 0x2BC730) takes blocks over 256 bytes through FUN_00150530 (VCS 0x2BC7E8), whose fallback (heap+4) is the
    // "MAIN MEMORY ALLOCATION FAILURE" handler 0x1F3318.
    PS2Runtime::RecompiledFunction g_origLarge = nullptr, g_origOom = nullptr;

    PS2_GUEST_CC void traceLarge(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t root = 0, used = 0;
        const uint32_t heap = GPR_U32(ctx, 4) & 0x01FFFFFFu;
        std::memcpy(&root, rdram + heap + 8, 4);
        std::memcpy(&used, rdram + heap + 0x14, 4);
        std::fprintf(stderr, "[alloc] large heap=0x%x size=0x%x ra=0x%x root=0x%x used=0x%x\n", GPR_U32(ctx, 4),
                     GPR_U32(ctx, 5), GPR_U32(ctx, 31), root, used);
        g_origLarge(rdram, ctx, runtime);
    }

    PS2_GUEST_CC void traceOom(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::fprintf(stderr, "[alloc] OUT OF MEMORY handler: size=0x%x ra=0x%x\n", GPR_U32(ctx, 5), GPR_U32(ctx, 31));
        g_origOom(rdram, ctx, runtime);
    }

#if defined(_WIN32)
    // ---- LCS_HWBP=<guest addr>[,<guest addr>]: a hardware write breakpoint (x86 debug registers) on EE RAM, in every
    // host thread - catches writers the guest write watch (VCS_WATCH) cannot see: runtime stubs, DMA, host threads.
    // A thread re-applies it every 5 ms (threads started later get it too); a hit prints the writing thread and RIP
    // (resolve with llvm-symbolizer / addr2line on lcs.exe, the printed base is lcs.exe's). Debugging only.
    uintptr_t g_hwbp[2] = {};
    uintptr_t g_exeBase = 0;
    PS2Runtime *g_hwbpRuntime = nullptr;

    LONG CALLBACK hwbpHandler(EXCEPTION_POINTERS *e)
    {
        if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
            return EXCEPTION_CONTINUE_SEARCH;
        CONTEXT *c = e->ContextRecord;
        if (!(c->Dr6 & 0xF))
            return EXCEPTION_CONTINUE_SEARCH;
        const int slot = (c->Dr6 & 1) ? 0 : 1;
        uint32_t value = 0;
        std::memcpy(&value, reinterpret_cast<const void *>(g_hwbp[slot]), 4);
        uint32_t pc = 0, ra = 0;
        if (g_hwbpRuntime)
        {
            pc = g_hwbpRuntime->cpu().pc;
            R5900Context *cpu = &g_hwbpRuntime->cpu();
            ra = GPR_U32(cpu, 31);
        }
        std::fprintf(stderr, "[hwbp] write to slot %d now 0x%08x by thread %lu rip=0x%llx (exe+0x%llx) guest pc=0x%x ra=0x%x\n",
                     slot, value, GetCurrentThreadId(), static_cast<unsigned long long>(c->Rip),
                     static_cast<unsigned long long>(c->Rip - g_exeBase), pc, ra);
        c->Dr6 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    void applyHwbpToThread(DWORD tid)
    {
        if (tid == GetCurrentThreadId())
            return;
        HANDLE h = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, tid);
        if (!h)
            return;
        if (SuspendThread(h) != static_cast<DWORD>(-1))
        {
            CONTEXT c{};
            c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(h, &c))
            {
                c.Dr0 = g_hwbp[0];
                c.Dr1 = g_hwbp[1];
                // L0 / L1 on, RW = 01 (write), LEN = 11 (4 bytes) for each slot in use
                DWORD64 dr7 = 0;
                if (g_hwbp[0])
                    dr7 |= 1ull | (1ull << 16) | (3ull << 18);
                if (g_hwbp[1])
                    dr7 |= (1ull << 2) | (1ull << 20) | (3ull << 22);
                c.Dr7 = dr7;
                SetThreadContext(h, &c);
            }
            ResumeThread(h);
        }
        CloseHandle(h);
    }

    void startHwbp(uint8_t *rdram, const char *spec)
    {
        uint32_t a = 0, b = 0;
        if (std::sscanf(spec, "%x,%x", &a, &b) < 1)
            return;
        g_hwbp[0] = reinterpret_cast<uintptr_t>(rdram + (a & 0x01FFFFFCu));
        g_hwbp[1] = b ? reinterpret_cast<uintptr_t>(rdram + (b & 0x01FFFFFCu)) : 0;
        g_exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        AddVectoredExceptionHandler(1, hwbpHandler);
        std::fprintf(stderr, "[hwbp] armed guest 0x%x 0x%x (exe base 0x%llx)\n", a, b, static_cast<unsigned long long>(g_exeBase));
        std::thread([] {
            const DWORD pid = GetCurrentProcessId();
            for (;;)
            {
                HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
                THREADENTRY32 te{};
                te.dwSize = sizeof(te);
                if (snap != INVALID_HANDLE_VALUE && Thread32First(snap, &te))
                    do
                        if (te.th32OwnerProcessID == pid)
                            applyHwbpToThread(te.th32ThreadID);
                    while (Thread32Next(snap, &te));
                if (snap != INVALID_HANDLE_VALUE)
                    CloseHandle(snap);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }).detach();
    }
#endif

    PS2Runtime::RecompiledFunction g_origVecPush = nullptr;

    // FUN_0020D188: pushes a refcounted object onto the vector at 0x42A9D8 (begin / end / cap) - logged on entry
    PS2_GUEST_CC void traceVecPush(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t v[3], first = 0;
        std::memcpy(v, rdram + 0x0042A9D8u, sizeof(v));
        if (v[0] && v[0] < 0x02000000u)
            std::memcpy(&first, rdram + v[0], 4);
        std::fprintf(stderr, "[vec] push: begin=0x%x end=0x%x cap=0x%x *begin=0x%x ra=0x%x\n", v[0], v[1], v[2], first,
                     GPR_U32(ctx, 31));
        g_origVecPush(rdram, ctx, runtime);
    }

    // The runtime's own guest allocations (guestMalloc: sceGsResetGraph's 128-byte packet, font / SIF packets, MPEG
    // callback data...) away from the game's heap. SetupHeap gave the runtime the game's heap region (the ELF's end up),
    // which LCS's allocator (0x406D50, from 0x673168) uses whole: sceGsResetGraph, called once the heap was in use, wrote
    // its packet over a vector's array at 0x673180.. (a pointer became 1, free(1) put a bogus node in the free tree, the
    // 32 KB text buffer came back as address 1 and the text loader spun on the legal screen for ever). Found with
    // LCS_HWBP (the writer was the runtime, invisible to VCS_WATCH). The EE kernel's area below the callback stacks
    // (0x80000) is never the game's: 0x40000..0x78000. LCS_RUNTIME_HEAP=0 keeps the old placement.
    constexpr uint32_t kRuntimeHeapBase = 0x00040000u;
    constexpr uint32_t kRuntimeHeapLimit = 0x00078000u;

    void applyLcs(PS2Runtime &runtime)
    {
        std::fprintf(stderr, "[lcs] Liberty City Stories overrides\n");
        if (!envOff("LCS_RUNTIME_HEAP"))
        {
#if defined(_WIN32)
            _putenv_s("PS2X_RUNTIME_HEAP", "1");
#else
            setenv("PS2X_RUNTIME_HEAP", "1", 1);
#endif
            runtime.configureGuestHeap(kRuntimeHeapBase, kRuntimeHeapLimit);
            std::fprintf(stderr, "[lcs] runtime allocations at 0x%x..0x%x, not in the game's heap (LCS_RUNTIME_HEAP=0: as before)\n",
                         kRuntimeHeapBase, kRuntimeHeapLimit);
        }
#if defined(_WIN32)
        if (const char *bp = std::getenv("LCS_HWBP"); bp && *bp)
        {
            g_hwbpRuntime = &runtime;
            startHwbp(runtime.memory().getRDRAM(), bp);
        }
#endif
        if (envOn("LCS_TRACE_ALLOC"))
        {
            g_origVecPush = runtime.lookupFunction(0x0020D188u);
            if (g_origVecPush)
                runtime.replaceFunction(0x0020D188u, &traceVecPush);
            g_origLarge = runtime.lookupFunction(0x00150530u);
            g_origOom = runtime.lookupFunction(0x001F3318u);
            if (g_origLarge)
                runtime.replaceFunction(0x00150530u, &traceLarge);
            if (g_origOom)
                runtime.replaceFunction(0x001F3318u, &traceOom);
        }
        vcs::StreamLayout stream;
        stream.read = kStreamRead;
        stream.poll = kStreamPoll;
        stream.fn5 = kStreamFn5;
        stream.wait = kStreamWait;
        stream.head = kStreamHead;
        stream.readAccepted = 0u; // the model streamer (0x18F648) re-sends a read until it answers 0
        if (vcs::installStreamHooksAt(runtime, stream))
            std::fprintf(stderr, "[lcs] disc streaming native (VCS_NATIVE_STREAM=0: through the IOP)\n");
        // Sound is not native for LCS yet: the emulated IOP stays on, unless PS2X_IOP=0 (an experiment: no sound driver
        // answers then).
        vcs::retireIop(false);
        if (!envOn("LCS_MOVIES"))
        {
            if (runtime.replaceFunction(kMoviePlay, &returnZero))
                std::fprintf(stderr, "[lcs] movies skipped (LCS_MOVIES=1 plays them, emulated)\n");
            else
                std::fprintf(stderr, "[lcs] could not skip the movie player at 0x%x\n", kMoviePlay);
        }
        (void)envOff;
    }
}

PS2_REGISTER_GAME_OVERRIDE("GTA Liberty City Stories (SLUS-21423)", "SLUS_214.23", 0x001C6A60u, 0u, &applyLcs)
