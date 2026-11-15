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
5. **The runtime's fixed pools out of LCS's memory** (`LCS_RPC_POOLS=0` undoes; runtime change in
   PS2RECOMP_PATCHES.md, `PS2X_RPC_POOL_BASE`): skipping the intro cutscene hung about half the time (black screen,
   `CTimer::m_FrameCounter` stopped, the main loop alive), letting it play never did. The hang was a model converted
   from corrupted data: guest samples in a RenderWare list walk (0x31E820) that never got back to its head, reached
   from the model loader (0x18E4B8 / 0x18EC8C); `LCS_CHECK_LISTS` caught the list (owner 0x1F0EF5C, nodes pointing
   past RAM) - always inside model 171's buffer. `LCS_CHECK_READS` (each streamed file compared with the disc before
   its conversion) showed 22-56 bytes changed in it, always at the absolute address 0x1F1008C, with values of the
   sound driver's RPC client (0x3F60C0, 1444 = its buffer size): the runtime's emulated `sceSifBindRpc` /
   `sceSifCallRpc` keep their server structures at a fixed 0x1F10000 (RPC packets 0x1F00000, TLS 0x1F20000, boot mode
   0x1F30000), in the middle of LCS's streaming heap (0x634140). Whether the write landed on a live part of a model
   depended on where the model was placed - hence one run in two. Now packed at 0x6C000..0x77000 (the EE kernel's
   area, below the runtime's heap move): 11 of 11 skips fine. Ruled out on the way: the native streaming's timing
   (`VCS_STREAM_SYNC=1`, `VCS_STREAM_DELAY_MS`), the IOP's writes (`PS2X_TRACE_IOP_WRITES`), the VU0 natives
   (`PS2X_VU0_NATIVE=0`), the HLE renderer (`PS2X_HLE=0`), the pad report (`PS2X_PAD_TRACE`).

Open:

- **IOP modules loaded twice** at boot (`[IOP] module arena exhausted`, MCMAN falls back to HLE): LCS reboots the IOP
  with IOPRP300.IMG between two loads; the runtime's reboot does not unload the first set. Harmless so far.
- **Recompiler**: resuming at the delay slot of an unconditional branch continues at the fall-through, not the target
  (seen in FUN_0020D188's generated code at 0x20D29C; the normal path is right). Latent.
- Sound (emulated IOP: unchecked by ear), saves, missions beyond the first, frame rate, widescreen, controls: none of
  VCS's work applies until its addresses are found (`tools/xmatch.py` first, then by hand).

## Addresses found (LCS <- VCS)

How each was found: `xglobal` = `tools/xglobal.py --vcs-lcs` (the code around VCS's uses, found in LCS; the votes
agree), `scan` = memory snapshots of a running game, `decomp` = `tools/eedecomp.py`.

| What | LCS | VCS | How |
|---|---|---|---|
| `CWorld::Players[0].m_pPed` (a player is 100 words: Players[i] at +i*400) | 0x408E40 | 0x4E4910 | xglobal (2 sites), decomp |
| `CWorld::PlayerInFocus` (byte) | 0x3D9BC8 | | decomp of 0x107278 |
| `FindPlayerVehicle` | 0x107278 | | decomp: Players[focus] -> +0x538 ? +0x534 : 0 |
| ped: position | +0x30 | +0x30 | decomp 0x107170 |
| ped: `m_pMyVehicle` / in a vehicle (word) | +0x534 / +0x538 | +0x480 / state +0x8C4 == 0x37 | decomp 0x107278 |
| `TheCamera` | 0x43BBF0 | 0x6F44D0 | xglobal (42 sites agree); +0x30 its position (0x107390) |
| `CPad` 0 | 0x408C60 | 0x5147A8 | xglobal (4 sites) |
| `CTimer::m_snTimeInMilliseconds` | 0x3D9B70 | 0x4CD104 | scan (+~1000 / s), 637 code uses (VCS 581) |
| `CTimer::m_FrameCounter` | 0x3D9B6C | 0x4CD124 | scan (+1 a frame, 30 / s at the PS2's rate) |

CTimer's code changed between the games (xglobal finds none of its sites); the rest of CTimer is around
0x3D9B6C..0x3D9BC8 (much-used words at 0x3D9B8C, 0x3D9BA0, 0x3D9BC0: the time step / scale candidates, unchecked).

## Controls (2026-11-14)

The VCS port's bindings (`src/game/vcs_input.cpp`: keyboard / mouse / gamepad -> the DualShock 2 the game reads) are
on for LCS. Its game addresses are a `vcs::input::GameLayout` (VCS's by default), LCS's set in `lcs_overrides.cpp`;
what is 0 there is off. Working: every binding (the F2 menu's Keyboard & mouse and Gamepad pages), the gamepad with
its options, the mouse as the right stick (once a game frame: the frame counter), the Driving keys (W / S / Space in a
vehicle: the player's vehicle fields). Not yet: the mouse turning the on-foot camera itself and the scopes (TheCamera's
CCam layout unchecked), the radio and weapon changes on the wheel (vehicle radio field; camera mode), free aim, the
car camera, the pause menu's mouse (VCS-only modules, `GameLayout::extras` off).

## Performance (2026-11-15)

The game ran in slow motion: 23 vblanks a second (the PS2: 60), so ~38% of real time - the frame took 43 ms, of
which the GS stage 37 ms (`perf`: `gs_ms`, `worker_ms`, `tex_decode_us`). Three fixes, measured at the first
mission (hidden run, ThinLTO):

| | vblanks / s | frame ms | game ms | render worker ms | GS stage ms | texture decode ms |
|---|---|---|---|---|---|---|
| quick build (none), before | 22.9 | 43.6 | 18.8 | 42.8 | 37.3 | ~31 |
| ThinLTO (`--opt lto`) | 25.3 | 39.5 | 19.7 | 38.8 | 37.8 | ~31 |
| + textures keyed by content | 35.6 | 28.1 | 4.0 | 27.5 | 6.0 | 0.03 |
| + VU1 microcode compiled | 60.0 | 16.7 | 3.2 | 4.8 | 5.3 | 0.03 |

- **Textures**: LCS re-sends its textures every frame in another format than they are read in (T4 as CT16, T8 as
  CT32) and loads the CLUT into one of the texture's pages right after; the GS backend's cache keyed such textures
  by page generations and missed every time (~25 MB decoded a frame). Keyed by the upload's content now
  (`PS2X_HW_CONTENT_KEY_ANY`, runtime; set by `lcs_overrides.cpp`, `LCS_TEX_CONTENT_KEY=0` undoes). VCS avoided this
  with hooks that name textures by the game's raster.
- **VU1**: `game/lcs/vu1_uploads.txt` was empty, so every VU1 microprogram was interpreted. The 23 uploads LCS makes
  (`PS2X_LOG_MPG_SOURCES=1` through the intro and the first mission, walking and driving) are listed there now;
  `tools/vu1_recomp.py` compiles them (50 programs, 2970 pairs; `tools/recompile.py` runs it).
- The game itself now runs as on the PS2: 30 game frames a second, its clock at 1000 ms a second.

## Frame rate (2026-11-15)

The VCS port's frame-rate module (`src/game/vcs_framerate.cpp`) runs on LCS with its addresses (`vcs::FrameLayout`,
set in `lcs_overrides.cpp`): the settings (F2 -> Display: **Game timing**, **Frame rate cap**) work as in VCS -
frame-rate independent at 60 by default, up to 240. LCS's addresses (VCS's in brackets):

| What | LCS | How |
|---|---|---|
| `CTimer::Update` | 0x2B4CF8 (0x2F4960) | the only caller of the timer read |
| kernel timer read | 0x368ED8 (0x451C00) | xmatch (unique) |
| VBlank counting callback (`*(obj+4) += 1`) | 0x2B5528 (0x317AC8) | xmatch (2 identical leaves; the one the VBlank chain calls) |
| the limiter | 0x2B5400 (0x3179A0); its `WaitVBlank` 0x2D9360, called at 0x2B54A8 (ra 0x2B54B0), object in `$s2` | xmatch, mipsdis |
| CTimer's last ticks (64-bit) / frame ticks / time scale / step / step not clipped | 0x3D72D0 / 0x3DA0D8 / 0x3D9CE4 / 0x3D9B8C (`$gp`+0x2C9C, gp 0x3D6EF0) / 0x3D9D9C | decomp of Update |

Differences from VCS, and the instruction patches they needed (`game/lcs/overrides.toml`, full recompile):

- LCS clamps the step to 0.5 inline in Update (no call to hook): the constant (`lui $at, 0x3F00` at 0x2B4EC0) is
  0.208 (1/240 s) instead. Checked: a forced 1/144 s step (`VCS_TIMESTEP=144`) gives 6.95 ms of game time a frame.
- The dead `step < 0.5` branches above 100 fps, found by pattern (`lui 0x3F00` -> `c.lt.s` -> `mul.s` -> `lui 0x3F80`
  -> `div.s`; the same search finds VCS's patched site): the ped's animation movement (0x1D5978, VCS 0x1F2158) - at
  144 fps Toni walked a third of the distance and the intro left him 13 m from where it does at 30 - and the cutscene
  objects' (0x2B75A8, VCS 0x31B19C). Both 0.001 as in VCS.
- Measured at the first mission (W for 1 s and the stop; the car accelerating 3 s): 30 fps 4.3 m / 27.6 m, 60 fps
  4.05 m / 26.9 m, 144 fps 3.5 m / 25.8 m (VCS saw sprinting 5-11% slower at 200-240 fps).
- Not ported yet (each a VCS fix whose LCS twin is not found): the HUD's flashing on the frame counter (2D pass), the
  wheels' rolling resistance per frame (VCS `CVehicle::ProcessWheel` + `vcs_physics_timing.cpp`: cars coast to a stop
  faster at high frame rates - LCS has no function with VCS's constants together), cars / bikes in water, boats'
  rudder, bike lean, moving platforms, the follow cameras' automatic turn (`max(step, 1.0)`), and the per-frame
  effects (particles, mission meters, lightning...: `src/game/vcs_*_timing.cpp`). `tools/stepscan.py` lists LCS's
  step comparisons to start from.

Speed: uncapped 130 game frames a second at the first mission (hidden run); the GS stage is the limit (texture uploads
and their hashing). `soleQueuedWriter` (runtime) walked every queued writer of each block for each texture lookup; from
the newest, it stops at the first (GS stage time in `bindTexture` 19.5% -> 3.9%).

## Widescreen (2026-11-15)

F2 -> Display: **Aspect ratio** (16:10, 16:9, 21:9, 32:9, Match window) and **HUD** (Keep 4:3 / Stretch) work as in
VCS, Hor+: the camera's view-window setter 0x113478 (VCS FUN_001AB9E0, xmatch: identical code; `RwCameraSetViewWindow`
0x100578 = VCS 0x18F2D8) is wrapped and widens the horizontal extent (`vcs::installViewWindowHook`); the presenter and
the renderer's HUD squeeze (far-plane 2D kept at 4:3) are the runtime's. Checked at 16:9 at the first mission: the view
wider, nothing stretched, the HUD at 4:3 in the middle, trees drawn to both edges.

Not yet: entity culling's side planes (VCS widens `CCamera::IsSphereVisible`'s, FUN_003816F8 at cam+0xB60 / +0xB70;
without it VCS lost buildings, peds and cars in the outer eighths of a 16:9 picture - LCS's twin is not found by its
VCS code shape: watch the edges in the city); HUD **Screen edges** (VCS names its HUD's draws, `vcs_hud.cpp`: on LCS
it behaves as Keep 4:3); the game's own Vert- widescreen option and the cutscene bars setting.

## Picture sharpness (2026-11-15, to look at again later)

The user finds the picture blurry. Checked with crops of the same spot at 1x / 2x / 3x and in the user's own 1440p
screenshots: the internal resolution applies (targets at the window's size with 8x MSAA; geometry edges antialiased and
much finer than at 1x); not the cause - the native post effects, the PCRTC (one circuit shown), the frame copy (1:1),
the mip levels, anisotropic filtering, point filtering (no game texture is point-filtered: all bilinear). What stays
soft / blocky is the textures: LCS's are tiny (a PSP game). Three options were tried on 2026-11-15 - a contrast-adaptive
sharpening pass, a motion blur setting and texture upscaling at creation (xBR corner rule + bilinear, 2x / 4x) - and
taken out again at the user's request (they did not help enough, and the game stuttered in the user's play-through:
the upscaling adds decode time when textures stream in). The commits stay in the history (reverted) to start from.
What would really change it is replacement textures.

## Tools for LCS debugging

- `tools/xglobal.py --vcs-lcs 0xADDR...`: a VCS global's LCS address, from the code around its uses (votes).
- `tools/mipsdis.py ELF LO HI`: a small MIPS listing (no capstone). `tools/stepscan.py`: the time step's compares /
  clamps. `tools/xpatch.py`: VCS's instruction patches looked for in LCS (fails where the code generation differs).
- Control channel (`tools/gamectl.py`): `eethreads` (the EE kernel's threads: status, wait, pc / ra), `streamstate`
  (native streaming: reads, polls, waits, pending per channel), `padstate`, `peek` / `peekf`.
- `PS2X_PAD_TRACE=1`: every change of the pad report; `LCS_CHECK_SECTORS=1`: the world sectors' entity lists checked.
- `LCS_CHECK_LISTS=1`: 0x31E820's lists followed first (a list that never returns to its head logged with the guest
  call chain; every 100,000th walk's chain too). `LCS_CHECK_READS=1`: each streamed model file compared with the disc
  before conversion. `PS2X_TRACE_IOP_WRITES=1`: the IOP's writes into EE memory. `VCS_STREAM_SYNC=1` /
  `VCS_STREAM_DELAY_MS=n`: native stream reads done at once / slowed (experiments).

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
- 2026-11-14: controls - the bindings on with LCS's addresses (`vcs::input::GameLayout`): keys, gamepad, mouse
  (as the right stick), the Driving keys (checked: W walks; F enters the car; W accelerates, A steers). Found the
  intermittent hang when the intro cutscene is skipped. `tools/xglobal.py`.
- 2026-11-15: the skip hang fixed (step 5 above): the runtime's RPC / TLS pools moved out of LCS's streaming memory.
- 2026-11-15: full speed (60 vblanks / s, was 23): ThinLTO, textures keyed by content, VU1 microcode compiled.
- 2026-11-15: a hang while driving to Chinatown (the user's report): `[guest-branch:missing-target] ... JAL
  target=0x1833c0`, thread 1 dormant - a function only called directly (from 0x17ED2C) that Ghidra left out. Every
  `jal` target and every code pointer in the executable checked against the map: that was the only one outside a
  mapped function (0x200C00's caller is data; 0x151040 / 0x3601BC are an epilogue and alignment). Added to
  `game/lcs/manual_functions.csv`.
- 2026-11-15: frame rate above 30: VCS's frame-rate module at LCS's addresses, the step floor and two dead-branch
  patches; 60 / 144 checked against 30. Uncapped ~130 fps.
