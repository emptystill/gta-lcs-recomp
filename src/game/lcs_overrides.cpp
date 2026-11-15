// Game-specific runtime bindings for Liberty City Stories (SLUS_214.23), applied when the ELF loads - the LCS
// counterpart of vcs_overrides.cpp (which only applies to VCS's executable). Each hook here is an LCS address with
// the VCS one it mirrors and how it was found (docs/LCS.md).
#include "game_overrides.h"
#include "debug/rig.h"
#include "game/vcs_display.h"
#include "game/vcs_framerate.h"
#include "game/vcs_input.h"
#include "game/vcs_stream.h"
#include "runtime/ps2_disc_image.h"

#include <algorithm>
#include <vector>
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

    // LCS_CHECK_SECTORS=1: before each walk of a world sector's entity list (0x18AC98: for each entity not flagged off,
    // the virtual at vtable(+0x5C)+0x44), the list checked from the host: an entity whose vtable or that method is not
    // code is logged (the freeze after a skipped cutscene: thread 1 called address 0 from 0x18ACEC).
    PS2Runtime::RecompiledFunction g_origSectorWalk = nullptr;
    uint32_t ld32(const uint8_t *rdram, uint32_t a)
    {
        uint32_t v;
        std::memcpy(&v, rdram + (a & 0x01FFFFFCu), 4);
        return v;
    }
    PS2_GUEST_CC void checkSectorWalk(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t list = GPR_U32(ctx, 4);
        uint32_t node = ld32(rdram, list);
        for (int n = 0; node && n < 100000; ++n)
        {
            const uint32_t entity = ld32(rdram, node), next = ld32(rdram, node + 8);
            const uint32_t vtbl = entity ? ld32(rdram, entity + 0x5C) : 0;
            const uint32_t method = vtbl >= 0x100000u && vtbl < 0x2000000u ? ld32(rdram, vtbl + 0x44) : 0;
            if (!entity || method < 0x100000u || method >= 0x400000u)
            {
                static int logged = 0;
                if (logged++ < 40)
                {
                    std::fprintf(stderr, "[lcs] sector list 0x%x (ra 0x%x): node 0x%x entity 0x%x vtable 0x%x method 0x%x next 0x%x"
                                         " | entity words:", list, GPR_U32(ctx, 31), node, entity, vtbl, method, next);
                    for (uint32_t w = 0; w < 0x70 && entity; w += 4)
                        std::fprintf(stderr, " %08x", ld32(rdram, entity + w));
                    std::fprintf(stderr, "\n");
                }
            }
            node = next;
        }
        g_origSectorWalk(rdram, ctx, runtime);
    }

    // LCS_CHECK_LISTS=1: 0x31E820(owner, callback, data) walks the linked list at owner+8 (node = next pointer; the
    // item is node - 0x1C) until it is back at the head. Before each walk the list is followed from the host: one that
    // never comes back to its head (the hang after a skipped cutscene) is logged with its nodes and the caller.
    PS2Runtime::RecompiledFunction g_origListWalk = nullptr;
    PS2_GUEST_CC void checkListWalk(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t owner = GPR_U32(ctx, 4), head = owner + 8;
        uint32_t node = ld32(rdram, head);
        int n = 0;
        for (; node != head && n < 20000; ++n)
        {
            if (node < 0x100000u || node >= 0x2000000u)
                break;
            node = ld32(rdram, node);
        }
        if (n >= 20000 || (node != head && (node < 0x100000u || node >= 0x2000000u)))
        {
            static int logged = 0;
            if (logged++ < 20)
            {
                std::fprintf(stderr, "[lcs] list walk 0x31E820: owner 0x%x never back at its head (ra 0x%x, callback 0x%x, "
                                     "stopped at 0x%x after %d) - nodes:", owner, GPR_U32(ctx, 31), GPR_U32(ctx, 5), node, n);
                uint32_t p = ld32(rdram, head);
                for (int i = 0; i < 12 && p >= 0x100000u && p < 0x2000000u; ++i, p = ld32(rdram, p))
                    std::fprintf(stderr, " %x(prev %x)", p, ld32(rdram, p + 4));
                // the guest stack's words that look like return addresses: the call chain
                std::fprintf(stderr, " | stack code words:");
                const uint32_t sp = GPR_U32(ctx, 29);
                for (uint32_t w = 0; w < 0x400; w += 4)
                {
                    const uint32_t v = ld32(rdram, sp + w);
                    if (v >= 0x100000u && v < 0x3D9000u && (v & 3) == 0)
                        std::fprintf(stderr, " sp+%x=%x", w, v);
                }
                std::fprintf(stderr, " | regs s0-s7:");
                for (int r = 16; r <= 23; ++r)
                    std::fprintf(stderr, " %x", GPR_U32(ctx, r));
                std::fprintf(stderr, "\n");
            }
        }
        // every 100000th call, the guest's call chain (the stack's code words) and the frame counter: in a hang the
        // looping caller shows up again and again
        static uint64_t calls = 0;
        if (++calls % 100000 == 0)
        {
            std::fprintf(stderr, "[lcs] walk #%llu frame %u owner 0x%x ra 0x%x chain:", static_cast<unsigned long long>(calls),
                         ld32(rdram, 0x003D9B6Cu), owner, GPR_U32(ctx, 31));
            const uint32_t sp = GPR_U32(ctx, 29);
            for (uint32_t w = 0; w < 0x300; w += 4)
            {
                const uint32_t v = ld32(rdram, sp + w);
                if (v >= 0x100000u && v < 0x3D9000u && (v & 3) == 0)
                    std::fprintf(stderr, " %x", v);
            }
            std::fprintf(stderr, "\n");
        }
        g_origListWalk(rdram, ctx, runtime);
    }

    // LCS_CHECK_READS=1: before a streamed model file is converted in place (0x18E4B8(buffer, model)), the buffer is
    // compared with the disc read that filled it: a difference means something wrote over it in between.
    PS2Runtime::RecompiledFunction g_origConvert = nullptr;
    PS2_GUEST_CC void checkConvert(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t buffer = GPR_U32(ctx, 4) & 0x01FFFFFFu;
        unsigned lsn = 0, bytes = 0;
        static uint64_t checked = 0, bad = 0;
        if (vcs::lastReadInto(buffer, lsn, bytes) && bytes && buffer + bytes <= 0x02000000u)
        {
            std::vector<uint8_t> disc(bytes);
            IPS2DiscImage *image = ps2_disc::image();
            if (image && image->readBytes(static_cast<uint64_t>(lsn) * 2048u, bytes, disc.data()))
            {
                ++checked;
                uint32_t first = bytes, count = 0;
                for (uint32_t i = 0; i < bytes; ++i)
                    if (disc[i] != rdram[buffer + i])
                    {
                        first = std::min(first, i);
                        ++count;
                    }
                if ((count && bad++ < 30) || GPR_U32(ctx, 5) == 171u)
                {
                    std::fprintf(stderr, "[lcs] model %u at 0x%x (LSN 0x%x, %u bytes): %u bytes differ from the disc, the first at"
                                         " +0x%x (ra 0x%x) frame %u:", GPR_U32(ctx, 5), buffer, lsn, bytes, count, first,
                                 GPR_U32(ctx, 31), ld32(rdram, 0x003D9B6Cu));
                    int shown = 0;
                    for (uint32_t i = first; i < bytes && shown < 24; ++i)
                        if (disc[i] != rdram[buffer + i])
                        {
                            std::fprintf(stderr, " +%x:%02x->%02x", i, disc[i], rdram[buffer + i]);
                            ++shown;
                        }
                    std::fprintf(stderr, "\n");
                }
            }
        }
        if ((checked & 1023) == 1)
            std::fprintf(stderr, "[lcs] read check: %llu model files checked, %llu differed\n",
                         static_cast<unsigned long long>(checked), static_cast<unsigned long long>(bad));
        g_origConvert(rdram, ctx, runtime);
    }

    // The frame boundary (VCS: markFrameKick on its display-list kick): LCS's limiter 0x2B5400 (VCS FUN_003179A0), once
    // per rendered frame. Before the original only (hook rule 2): the rig's game-thread jobs (the control channel's
    // poke / dump / scm...: they never ran on LCS) and the hold while the window is in the background.
    PS2Runtime::RecompiledFunction g_origLimiter = nullptr;
    PS2_GUEST_CC void frameBoundary(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        vcs::rig::onGameFrame(rdram, ctx, runtime);
        vcs::input::holdInBackground(runtime);
        g_origLimiter(rdram, ctx, runtime);
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
    constexpr uint32_t kRuntimeHeapLimit = 0x0006C000u;
    // The runtime's fixed pools (SIF RPC packets / servers, TLS, boot mode: 0x1F00000.. by default) are in LCS's
    // streaming memory: the RPC server structures overwrote a model being converted - the hang after a skipped
    // cutscene (docs/LCS.md). Moved next to the runtime's heap: 0x6C000..0x77000 (PS2X_RPC_POOL_BASE).
    // LCS_RPC_POOLS=0 keeps them where they were.
    constexpr const char *kRpcPoolBase = "6C000";

    void applyLcs(PS2Runtime &runtime)
    {
        std::fprintf(stderr, "[lcs] Liberty City Stories overrides\n");
        // Textures re-sent every frame in another format than they are read in (T8 as CT32): keyed by the upload's
        // content, so the decoded copy is found again (PS2X_HW_CONTENT_KEY_ANY in the runtime; ~25 MB of textures were
        // decoded every frame, 30 ms of the GS stage). LCS_TEX_CONTENT_KEY=0: by page generations as before.
        if (!envOff("LCS_TEX_CONTENT_KEY"))
        {
#if defined(_WIN32)
            _putenv_s("PS2X_HW_CONTENT_KEY_ANY", "1");
#else
            setenv("PS2X_HW_CONTENT_KEY_ANY", "1", 1);
#endif
        }
        if (!envOff("LCS_RPC_POOLS"))
        {
#if defined(_WIN32)
            _putenv_s("PS2X_RPC_POOL_BASE", kRpcPoolBase);
#else
            setenv("PS2X_RPC_POOL_BASE", kRpcPoolBase, 1);
#endif
            std::fprintf(stderr, "[lcs] the runtime's RPC / TLS pools at 0x%s, not in the game's streaming memory"
                                 " (LCS_RPC_POOLS=0: as before)\n", kRpcPoolBase);
        }
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
        // The player's controls (game/vcs_input.h): the bindings, the gamepad and the mouse feed the pad, with LCS's
        // addresses for what the reads look at; what is not known yet stays 0 (off: docs/LCS.md "Controls").
        {
            vcs::input::GameLayout input;
            input.playerPed = 0x00408E40u; // CWorld::Players[0].m_pPed (VCS 0x4E4910; tools/xglobal.py)
            // FindPlayerVehicle 0x107278: m_pMyVehicle +0x534, m_bInVehicle +0x538 (a word; VCS +0x480 and the state
            // +0x8C4 == 0x37); the position at +0x30 as in VCS (0x107170)
            input.pedVehicle = 0x534u;
            input.pedState = 0x538u;
            input.pedStateDriving = 0;
            input.vehicleRadio = 0;
            input.theCamera = 0;           // 0x43BBF0 (VCS 0x6F44D0; xglobal); its CCam layout not checked yet
            input.scopeZoom = input.camStickLocked = input.camTable = input.camIndoors = input.camWallSkip = 0;
            input.pad0 = 0;                // 0x408C60 (VCS 0x5147A8; xglobal); DisablePlayerControls' offset unchecked
            input.frameCounter = 0x003D9B6Cu; // CTimer::m_FrameCounter (memory scan: +1 a frame, 30 a second)
            input.gameClock = 0x003D9B70u;    // CTimer::m_snTimeInMilliseconds (637 code references; VCS 581)
            input.userPause = input.codePause = 0;
            input.extras = false;
            // The direct mouse camera (found by memory diffs while the right stick turned the camera, and by writes the
            // game kept - docs/LCS.md "Mouse camera"): TheCamera (0x43BBF0) + 0x260 the active camera's mode (4 on foot,
            // 18 behind a vehicle: GTA III's FOLLOWPED / CAM_ON_A_STRING), +0x2F4 / +0x310 the heading and its target,
            // +0x2F8 the pitch and +0x300 its negation (the target); the stick raises the pitch to 1.466 at most.
            // three CCams (GTA III's Cams[3]) at +0x260, +0x510, +0x7C0 (modes found at each), the active one's index a
            // byte at +0x50 (as VCS's); the fields below are inside a CCam
            input.directCamera = 0x0043BBF0u; // TheCamera (xglobal: VCS 0x6F44D0)
            input.directActive = 0x50u;
            input.directCam0 = 0x260u;
            input.directCamStride = 0x2B0u;
            input.directMode = 0x0u;
            input.directBeta = 0x94u;
            input.directBetaTarget = 0xB0u;
            input.directAlpha = 0x98u;
            input.directAlphaNeg = 0xA0u;
            input.directAlphaMin = -0.07f;  // the game's own limit on foot (looking up)
            input.directAlphaMax = 1.466f;  // the stick's
            input.directCarAlphaMin = -0.07f;
            input.directCarAlphaMax = 1.30f;
            vcs::input::setGameLayout(input);
        }
        if (envOn("LCS_CHECK_READS"))
        {
            g_origConvert = runtime.lookupFunction(0x0018E4B8u);
            if (g_origConvert && runtime.replaceFunction(0x0018E4B8u, &checkConvert))
                std::fprintf(stderr, "[lcs] checking streamed model files against the disc (LCS_CHECK_READS)\n");
        }
        if (envOn("LCS_CHECK_LISTS"))
        {
            g_origListWalk = runtime.lookupFunction(0x0031E820u);
            if (g_origListWalk && runtime.replaceFunction(0x0031E820u, &checkListWalk))
                std::fprintf(stderr, "[lcs] checking 0x31E820's lists (LCS_CHECK_LISTS)\n");
        }
        if (envOn("LCS_CHECK_SECTORS"))
        {
            g_origSectorWalk = runtime.lookupFunction(0x0018AC98u);
            if (g_origSectorWalk && runtime.replaceFunction(0x0018AC98u, &checkSectorWalk))
                std::fprintf(stderr, "[lcs] checking the world sectors' entity lists (LCS_CHECK_SECTORS)\n");
        }
        // Frame rate (game/vcs_framerate.h): 30 / 60 in original timing, up to 240 with independent timing. LCS's
        // CTimer::Update (0x2B4CF8) clamps its step to 0.5 inline (no call to hook as in VCS): the constant is patched
        // in game/lcs/overrides.toml instead (0x2B4EC0). docs/LCS.md "Frame rate".
        {
            vcs::FrameLayout frame;
            frame.countVBlank = 0x002B5528u;  // *(obj + 4) += 1 from the VBlank callback (VCS 0x317AC8)
            frame.timerTicks = 0x00368ED8u;   // xmatch: VCS 0x451C00, its only caller is CTimer::Update
            frame.timerEnsure = 0;            // none: the clamp is inline
            frame.afterMinClamp = 0;
            frame.waitVBlank = 0x002D9360u;   // the limiter's VBlank wait (0x2B5400 = VCS 0x3179A0, xmatch)
            frame.limiterWait = 0x002B54B0u;  // its return address (jal at 0x2B54A8)
            frame.limiterObjectReg = 18u;     // $s2, as in VCS (0x2B5410: daddu s2, a0, zero)
            frame.timerUpdate = 0x002B4CF8u;
            frame.twoDPass = 0;               // the HUD's flashing: not found yet
            frame.frameCounter = 0x003D9B6Cu;
            frame.timeMs = 0x003D9B70u;
            frame.prevTicks = 0x003D72D0u;    // 64-bit (Update: frame ticks = now - prev; prev = now)
            frame.frameTicks = 0x003DA0D8u;
            frame.timeScale = 0x003D9CE4u;
            frame.timeStep = 0x003D9B8Cu;     // 1.0 = 1/50 s ($gp + 0x2C9C)
            frame.timeStepNonClipped = 0x003D9D9Cu;
            vcs::installFrameRateHooks(runtime, frame);
        }
        // Widescreen (Hor+, game/vcs_display.h): the camera's view-window setter 0x113478 is VCS's FUN_001AB9E0 (xmatch,
        // identical code); the settings' aspect widens its horizontal extent. The presenter and the renderer's HUD
        // squeeze are the runtime's. Entity culling's side planes (VCS's CCamera::IsSphereVisible) not found yet.
        if (vcs::installViewWindowHook(runtime, 0x00113478u))
            std::fprintf(stderr, "[lcs] widescreen: view window hook at 0x113478\n");
        g_origLimiter = runtime.lookupFunction(0x002B5400u);
        if (g_origLimiter)
            runtime.replaceFunction(0x002B5400u, &frameBoundary);
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
