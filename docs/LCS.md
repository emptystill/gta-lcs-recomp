# Liberty City Stories on the VCS port

This tree is a copy of the GTA: Vice City Stories native port (2026-11-14), turned towards GTA: Liberty City Stories
(PS2, USA, SLUS-21423 v1.01). The two games share Rockstar Leeds' engine (LCS is the older one) and the same Sony SDK
build, so the recompiler, the runtime, the platform layer and most tools carry over unchanged; the game code in
`src/game/` knows VCS's addresses and is off for LCS until each part has its LCS addresses.

An independent project since (docs/DECISIONS.md): the game is `lcs.exe` / `lcs.ini`, the launcher `LCSLauncher`. The
VCS port's documentation is in `docs/vcs-reference/`, kept as the map of what to look for: the same kinds of functions,
data and fixes are expected in LCS at other addresses.

## The disc

- `SYSTEM.CNF`: `BOOT2 = cdrom0:\SLUS_214.23;1`, `VER = 1.01`, NTSC. The executable is 2,994,708 bytes, SHA-1
  `9b021b4c4e42087bb9b374709f26cf67744b75ab` (VCS's: 3,993,368).
- The same IOP modules as VCS (`MODULES/`: CDSTREAM, CDVDSTM, IOPRP300.IMG, LIBSD, MCMAN, MCSERV, PADMAN, SDRDRV,
  SIO2MAN); CDSTREAM.IRX is 42,320 bytes (VCS 48,540), the others have VCS's sizes.
- Differences from VCS's disc: textures are `.CHK` (VCS `.XTX`): `SPLASH/*.CHK`, `CHK/PS2/GAME.DTZ`; no `FRONTEND/*.PSP`
  menu files (the menus live in the code); data in folders: `DATA/MAIN.SCM` (VCS: the root), `MODELS/*.IMG` + `.LVZ`
  per area (COMMER, INDUST, SUBURB, UNDERG; VCS: BEACH, MAINLA, MALL), `TEXT/ENGLISH.GXT` only, `ANIM/CUTS.IMG`,
  movies `MOVIES/DM.PSS` and the three Rockstar logos (no INTRO / STINGS).
- Audio: `AUDIO/CUTSCENE/*.VB`, `AUDIO/MUSIC/*.VB` (the radio stations), `AUDIO/NEWS/*.VB`.

## The executable

- Ghidra 12.1.4 + ghidra-emotionengine-reloaded v2.1.39 (`tools/ghidra_analyze.py`, 92 s of analysis): 5,537
  functions; `tools/find_missing_functions.py` adds 377 that are only referenced indirectly (VCS: 9,550 + 833).
- ps2_analyzer: the same SDK libraries as VCS (libgraph, libdma, libpad, libcdvd with streaming, libmc, libmpeg / ipu,
  libsdr, libsif, libfileio, Deci2), 141 to stub. Their layout is VCS's, shifted: e.g. sceDmaSend 0x35FC48
  (VCS 0x448970), sceDmaPause 0x88 bytes later in both; scePadRead 0x360760 (VCS 0x449488).
- `game/lcs/overrides.toml`: the LCS twins of every VCS entry (TODO handlers, `_lf_bind`, libfileio's directory calls,
  libcdvd's cdvdfsv calls), found by `tools/xmatch.py` and confirmed by the analyzer's names.

## Tools added for LCS

- `tools/xmatch.py`: finds functions of one executable in the other by their code, link-time addresses masked (jump
  targets, `lui`, absolute low halves; stack offsets and branches kept). The way to carry VCS's hooks across: VCS
  address -> its function -> the same code in LCS.
- `tools/compat_symbols.py`: run by recompile.py; declares and stubs the VCS functions `src/` names directly (34) that
  LCS's generated code lacks, so `src/` still compiles and links. A stub that runs says `[compat] ... called` and stops.

## What is off for LCS, and why

- The VCS override block (`src/game/vcs_overrides.cpp`, `applyVcs`): registered for VCS's executable only, so it never
  applies. It installs every game hook: widescreen, HUD, frame rate, native rendering, native sound and streaming
  (the IOP is emulated for LCS), the pause menu, aim, cameras, timing fixes.
- Set off in `src/main.cpp`: the player's bindings (`VCS_INPUT_MAP=0`: the runtime's own pad mapping; the bindings also
  write VCS's camera / aim / radio memory), the game's volume levels (`VCS_GAME_VOLUME=0`), the trainer (not installed).
- Data folder: `%LOCALAPPDATA%\LCSRecomp` (VCS: `VCSRecomp`), so LCS never touches a VCS memory card.
- The launcher (`LCSLauncher`) names LCS; its look (sunset, palm trees, pink) is still Vice City's.

## Status (2026-11-14)

**The game boots and plays**: legal screen, loading screen, the intro cutscene (Toni arriving), then gameplay with
the HUD and the first mission's marker - the recompiled LCS code on the port's D3D11 renderer, everything else
emulated (no LCS-specific enhancement yet). Checked in hidden test runs only (screenshots), with the runtime's own pad
mapping (Shift = Cross, Enter = Start, arrows / WASD).

What it took, in order (each in `src/game/lcs_overrides.cpp` with its reason, `LCS_*` switches to undo):

1. **Movies skipped** (`LCS_MOVIES=1` plays them, emulated): `Play` 0x35A020 (VCS 0x40D8B8), found from the movie
   paths' references (`FUN_001F3728` loads RSNY / RSLEEDS / RSNORTH / DM.PSS). Emulated, the first logo never ended.
2. **Runtime allocations out of the game's heap** (`LCS_RUNTIME_HEAP=0` undoes; runtime change in
   PS2RECOMP_PATCHES.md): the legal screen stayed for ever. Chain, each step found with the tools below:
   the text loader (0x3326A8, `CText::Load`-like: TABL / TKEY / TDAT chunks of `TEXT\ENGLISH.GXT`) read the whole file
   without finding its sections - its 32 KB buffer (`FUN_00150E68(0x8000)`) was address **1** (`VCS_STREAM_TRACE`)
   - the heap (CMemoryHeap-like at 0x406D50 over 0x673168..0x1AFE000; large blocks in a size tree, `FUN_00150530`,
   OOM handler 0x1F3318 "MAIN MEMORY ALLOCATION FAILURE") had a free-tree node with children 0xFFFFFFF9 /
   0x10002400 (`LCS_TRACE_ALLOC`, `peek`) - written by `free(1)` (`VCS_WATCH` on the node, `VCS_WATCH_STACK`) from
   `FUN_0020D188` (a refcounted vector at 0x42A9D8, called from the GAME.DTZ loader `FUN_001045A8`) whose old array
   held 1 instead of a pointer - a write `VCS_WATCH` never saw: a **host** writer. `LCS_HWBP=673190` (x86 debug
   registers) caught it in the runtime's `sceGsResetGraph` stub, which builds its GIF packet with `guestMalloc` - and
   `SetupHeap` had made the runtime's heap the game's own heap region. VCS never hit it (its allocations there were
   not live yet when the stub ran, presumably).
3. **Disc streaming native, LCS's protocol** (`VCS_NATIVE_STREAM=0`: through the IOP): LCS's CDSTREAM differs from
   VCS's - an accepted read answers **0** (VCS: 1; the model streamer `FUN_0018F648` re-sends until 0), and two more
   client calls: fn 3 wait(channel) 0x1F6EA0, fn 4 head LSN 0x1F6DE8. `vcs::StreamLayout` (game/vcs_stream.h).
4. **A function only reached through a pointer**: 0x20CE98 (32 bytes, between two mapped functions; the log's
   `[guest-branch:missing-target] ... target=0x20ce98`, thread 1 dormant). `game/lcs/manual_functions.csv` (kept by
   make_config.py; find_missing_functions.py rewrites extra_functions.csv). Expect more of these: each is one line.

Open:

- **IOP modules loaded twice** at boot (`[IOP] module arena exhausted`, MCMAN falls back to HLE): LCS reboots the IOP
  with IOPRP300.IMG between two loads; the runtime's reboot does not unload the first set. Harmless so far.
- **Recompiler**: resuming at the delay slot of an unconditional branch continues at the fall-through, not the target
  (seen in FUN_0020D188's generated code at 0x20D29C; the normal path is right). Latent.
- Sound (emulated IOP: unchecked by ear), saves, missions beyond the first, frame rate, widescreen, controls: none of
  VCS's work applies until its addresses are found (`tools/xmatch.py` first, then by hand).

## Tools for LCS debugging

- `LCS_HWBP=<guest addr>[,<guest addr>]`: hardware write breakpoint on EE RAM in every host thread (Windows) - the
  writer's thread and RIP (`exe+0x...`; vcs.exe has no symbols for the runtime, read the code with llvm-objdump).
- `LCS_TRACE_ALLOC=1`: the heap's large-block path, the OOM handler and the vector push, on entry.
- `VCS_STREAM_TRACE=1`: every stream read (channel, dest, LSN, bytes, caller).
- `tools/eedecomp.py ADDR...`: works on the `lcs` Ghidra project (Ghidra / JDK in `build/ghidra-tools`, set
  GHIDRA_HOME / JAVA_HOME).

## Log

- 2026-11-14: tree copied, tools pointed at SLUS_214.23 / `game/lcs`, Ghidra + analyzer run, overrides found.
- 2026-11-14: legal screen (movies skipped) -> loading screen (runtime heap) -> intro cutscene and gameplay (LCS
  stream protocol, 0x20CE98).
- 2026-11-14: independent identity (docs/DECISIONS.md, phase 1): `lcs.exe`, `lcs.ini`, `LCSLauncher`, data in
  `LCSRecomp` (the launcher's own backups had still pointed at `VCSRecomp`), VCS docs to `docs/vcs-reference/`.
