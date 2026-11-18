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
  write VCS's camera / aim / radio memory), the game's volume levels (`VCS_GAME_VOLUME=0`). VCS's trainer is not built; LCS has its own ("Trainer" below).
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
VCS code shape: watch the edges in the city); the game's own Vert- widescreen option and the cutscene bars setting.

### Radio logos the pack redrew (2026-11-17)

The Audio page's Double Clef (texture 64 x 64) and The Liberty Jam stayed the PS2's: the pack's (and the frontend
pack's) images are the PSP's drawings - The Liberty Jam with a white rim and "THE", Double Clef in other colours - so
the difference (117 for The Liberty Jam) or the brightness's correlation (0.62 / 0.42) failed the rule. After the
rule, a 64 x 64 texture is tried against the Radio/ images by the alpha's and the brightness's correlations summed:
the best at 1.2 and 0.05 over the next (Double Clef 1.43 against 0.91, The Liberty Jam 1.23 against 1.16; the page's
particles and clouds reach 1.03 with some logo, the fist icon 0.86). The Liberty Free Radio logo before that had been
taken for a radar tile: map tiles are matched by structure at 128 x 128 only.

### More texture packs: the frontend (2026-11-17)

The user added two more packs: "Remastered Frontend PCSX2 [LCS]" (made for PCSX2 and this very disc, SLUS-21423: 141
menu / loading / HUD images) and "GTA LCS HD" (PPSSPP, the PSP's: interface plus 3D world textures). `tools/hud_pack.py
install` takes several packs now (the first one's image wins for the same path); installed: v2.1 x256, the frontend,
GTA LCS HD - 309 images.

- **PCSX2's names** are `<texture hash>-<CLUT hash>-<bits>`: the bits are TEX0's PSM (6 bits), TW and TH (4 each) - the
  game texture's size (0x5dd4: PSMT4 128x128; 0x2653: PSMT8 512x512). The probe is the image box-averaged to exactly that
  size, also when the image has another shape (1024x512 for a 512x512 loading screen; the shown image is then stretched
  to the texture's shape, as the runtime wants one scale for both sides).
- **PCSX2 keeps the GS's alpha** (128 opaque, its images go up to 128 - one to 166): converting it like a PPSSPP pack's
  halved it and nothing matched.
- **Where**: in a frame without 3D every textured 2D draw may take a pack image (the pause menu draws its text and
  frames depth-tested at the far plane, which the HUD-only condition left out), and the frame waits for the match (up
  to a second; ~30 ms once the pack is read, which now starts when the option is switched on): a loading screen draws
  its picture once and then loads with the frame standing still, so a later match never showed.
- **What matched** (2026-11-17, 1080p): the loading screens - the PSP pack's (same drawings as the PS2's: difference 15),
  the pause menu's backdrop, map, title font (Pricedown), its arrows and the HUD's numbers, health bar, radar centre and
  north from the frontend pack; fonts and the rest of the HUD from v2.1 as before. Not matched: the controller drawing
  on the Controls page (best 127), a few 64x64 / 16x16 icons.
- **Not used**: GTA LCS HD's 3D world folders (Buildings, Vehicles, Signs, World, Props, Pedestrians, Particles; skipped
  by the tool) - the world is drawn natively from the game's models, another path; and its "Alt" variants.

### Menus and loading screens (2026-11-17)

The user asked for the loading screens and menus to fill a 16:9 picture "like the game", the pictures enlarged and
cropped. Display: **Wide menus and loading screens** (`display.fill_2d`, off; `LCS_FILL_2D`; `gs_hw.cpp`
`gsHwSetFill2d`):

- **The shape.** Frames without 3D were shown at 4:3 between bars (`decideShape`, the 3D history). With the option they
  stay wide: their 2D is squeezed into its 4:3 shape in the middle as during play (far-plane 2D not covering the
  width), full-width draws reach the edges.
- **The pictures.** A loading screen is one 512x512 texture drawn at y -77..523 (the game crops it already), with the
  "Grand Theft Auto" logo painted into it; the pause menu's backdrop the same (-75..522, 256x256). A textured draw
  covering the whole 640 x 448 in a frame without 3D is enlarged in height by the widening, so it keeps its shape across
  the full width; about y 336 (three quarters down), not the middle.
- **The progress bar** (untextured, x 113..248, y 392..410, no scissor) belongs under the logo: drawn after such a
  picture without a scissor of its own it takes the same enlargement instead of the squeeze (squeezed it lay over the
  enlarged logo; enlarged about the middle it went below the screen - hence the pivot). The pause menu's draws are
  scissored to x 80..558, so they keep their 4:3 shape over the enlarged backdrop.
- **Changing page** the pause menu draws its text without that scissor for a few frames; the first rule (any
  unscissored draw follows) enlarged and stretched it with the backdrop - the user saw the menu "double" at each page
  change (found in a recording of their play, then 20 screenshots a second while changing page). Now only untextured
  draws follow, and only until something textured is drawn after the picture (the menu's text and icons; a loading
  screen draws nothing textured after its picture).
- **Zoomed further** (the user's next screenshot): the map's scissor opens to x 0..637 (y 0..367 still) while its dark
  box keeps the menu's x 80..558 - the PS2's own seam, a strip of unshaded map left of x 80. Full width now needs the
  whole height of scissor too (the map stays with the menu), and with the option an untextured draw reaching far past
  the menu's scissor gets the screen's (the box covers what the map covers).
- **The map zoomed in** (the user's screenshot, 2026-11-17): the map is one 512x512 picture drawn larger than the
  screen (x -750..449 and further) and clipped to the menu's x 80..558, with a dark box over the islands not yet open
  (untextured, alpha 0x6e). Once the picture spanned the width it counted as full-width - stretched, and enlarged as a
  backdrop - while the box was squeezed into 4:3: the two no longer matched. Full width now also needs a scissor
  covering the screen (`fullWidth` in `recordPrimitive`), so whatever the menu clips to its area keeps the 4:3 shape
  with it: the map, its box and its blips together, as on the PS2. Also true without the option.
- Found with the F12 draw list (`PS2X_HW_DUMP_KEY=1`) on the first loading screen and the pause menu's Controls page.
- Off, as before: a loading screen at 4:3 between bars; LCS's pause menu shows wide with its backdrop squeezed into
  the middle (the menu primitives are not named by a classifier here, so no switch to 4:3).

### HUD (2026-11-16)

The user reported the HUD and radar still at 4:3 in full screen, and the weapon icon out of place.

Display > HUD **Screen edges** now works on LCS. It uses VCS's `vcs_hud.cpp`, which records the HUD's 2D draws,
groups them into elements, anchors each to the edge it is near, and draws them on the native HUD layer. The module's
addresses became a `vcs::HudLayout` (`setHudLayout`), and LCS's are set in `lcs_overrides.cpp` (`LCS_HUD=0`: off).

| Routine | LCS | VCS | How it was found |
|---|---|---|---|
| RwIm2DRenderPrimitive | **0x2B1A70** | 0x2FBE08 | xmatch, unique |
| its indexed form | **0x2B1AD0** | 0x2FBE68 | xmatch, unique |
| VIF1 display-list kick | **0x212810** | 0x2723E8 | xmatch, unique; hooked to end the HUD context and anchor the frame's elements |
| CHud::Draw | **0x23F438** | | data: it calls the money (0x245730, "$%08d") and the clock (0x2457B0, "%02d:%02d"); called from the 2D pass **0x1F6338** |
| RwRenderStateSet | **0x144B58** | 0x175198 | the most-called function with a0 = 6 (ZTESTENABLE); the same code as VCS's, its jump table elsewhere |
| rect fill | **0x3219C0** | 0x3E0B10 | xmatch |

What the port does:

- **The weapon icon.** With "Keep 4:3", the icon sat apart from the cluster: it is drawn at its own z, outside the
  renderer's HUD plane (VCS's old bug, game-internals 6f). The HUD context now moves it onto the plane, so it is in
  place in every HUD mode.
- **The anchors.** The coordinates are 640x448 as VCS's. Traced: the clock / health / money / weapon cluster (x 480..626)
  goes to the right edge, the radar (26..126) to the left. The zone name stays on the right, and the pause menu is
  untouched (it is drawn outside CHud::Draw).
- **The scope mask.** The 2D pass draws two black rects through the rect fill on each of its two scope branches
  (returns 0x1F64B0 / 0x1F64E0 and 0x1F6520 / 0x1F6550). These are the PSP's bars, above and below the 480x272 screen,
  so they draw nothing. The mask sprite covers the whole 4:3 picture, so the world showed beside it. In widescreen the
  two rects become the side strips (`maskFromBars`).
  - Their corners are 2 units off the screen. On its corners they shared vertex positions with the mask sprite, and
    the renderer, which knows HUD vertices by position, squeezed half of each bar into a slant.
- **Not hooked:** the radar / font routines (the radar and centred text are known by position only), the menu and the
  loading screen (drawn as before).
- **A rule that was dropped.** Leaving draws half the screen wide or more out of the grouping split the scope's
  mask pieces to different edges. In a scope the cluster stays with the mask, in the middle.

**Checked** at 16:9, 1920x1080, with Screen edges: on foot, aiming, the sniper's scope (black to the edges), the
rocket launcher, the pause menu, getting into a car. With Keep 4:3: the icon in the cluster.


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

**Found (2026-11-15, the user: an emulator's upscaling looks sharp, no patches):** two mistakes of method first - the
hidden test window is 960x672 and the runtime composes the shown picture at the window's size, so 2560x1440 captures
had been upscaled from it (`PS2X_HIDDEN_WINDOW=2560x1440` for real comparisons). Then, with that: LCS's **trails**
effect (the runtime's native trails, the same as VCS's: downsample the frame into a 512-wide buffer, blend it back
through warped strips, add it tinted) is what softens the whole picture - `PS2X_HW_SKIP_TRAILS=1` gives a sharp
picture; the effect run as recorded (`PS2X_HW_POSTFX=bc`) is as soft as the native one. The softening is the warped
blend-back itself, not only the buffer's 512-wide resolution (now scaled with the frame, `PS2X_HW_TRAILS_SCALE`).
Graphics > **PS2 blur filter** (`display.ps2_blur`, `LCS_PS2_BLUR`; on by default, the original): off leaves the
blend-back out and keeps the tint - sharp, with the game's colours.

### Sharp HUD (2026-11-17)

The user asked for a better-looking HUD. At 1440p the clock, money, ammo, weapon icon and the radar's map and blips are
blurred: they are a PSP game's tiny textures (64x16 .. 256x256) stretched 3-4 times; the bars and the radar's ring are
shapes, already sharp.

Graphics > **Sharp HUD** (`display.hud_sharp`, off; `LCS_HUD_SHARP`): the native HUD layer draws those textures from
copies made 4x larger by **xBR** (runtime `gsHwSetHudUpscale`, `hudxbr::`):
- **The upscale.** xBR's level-1 rule, 2x twice: a texel becomes 2x2; a corner with an edge through it takes the mean
  of the texel and its nearer neighbour (smooth diagonals and curves), every other new texel is the texel itself (sharp
  edges). The Texture upscaling tried on 2026-11-15 (reverted) filled those with bilinear, which is no sharper than the
  original filtered.
- **Where.** Only draws of the HUD layer (`LayerTarget::hud`) from a game texture: the first time, the texture is read
  back from the GPU (`readTexture`, small) and upscaled on a helper thread (`std::async`); until it is ready, a frame
  or two, the original is drawn. The copies are dropped with their texture (TexDelete) and when the setting changes.
- **Sampling.** `gs_draw.hlsl` `sampleUpscaled` (linear mode 5, the full shader): bilinear at the copy's own texels,
  the game texture's wrap / clamp scaled by k - REGION_CLAMP keeps a font or icon cell's own texels, as `appendHudDraw`
  wants.

Measured (optimised build, 2560x1440, a drive across 8 districts): 60 fps with it on and off alike, render 0.1-0.8 ms
both. 34 copies made on the way (the radar's map tiles, 128x128, come as he moves), each ready ~10 ms after it was
first needed. Made on the render thread at first, they had cost ~5 ms a tile and ~25 ms for a 256x256: hitches -
hence the helper thread.

Checked: crops at 2560x1440 off / on (the clock, money, ammo, the AK's icon, the radar's map and its N): the edges and
outlines sharp, no texels of the next atlas cell along the quads' edges.

### HUD texture pack (2026-11-17)

The user gave a high-resolution interface made for PPSSPP ("GTA LCS HD+ Interface v2.1", x256: the PSP version
ULES00151) and asked for all of it, the weapon icons black and white (its "Original Icons"). Graphics > **HD HUD
textures** (`display.hud_pack`, off; `LCS_HUD_PACK`) draws from its images after `python tools/hud_pack.py install
<the pack's folder>` (into `<data>/hud_pack`; `status`, `remove`; `--icons pack` the coloured icons, `--no-maps`).

- **Why by content.** The pack names its images through PPSSPP's table of the PSP's texture hashes, unrelated to this
  game. And this game's texture keys are not the picture alone: the health bar's two pictures came with 64 keys in one
  session. So each texture is matched by content the first time it is drawn (`gs_hw.cpp` `hudpack::`), on a helper
  thread, against the pack's probes of its own size (each image box-averaged by 2, 4 and 8).
- **Alpha.** The PS2's full opacity is 128 (the GS doubles it), the pack's 255: `install` converts the images to the
  PS2's scale. Unconverted, every match was off by it and blending would take them as twice opaque.
- **The rule** (21 pairs checked by eye, 2026-11-17): the colour difference over the texels either picture shows (a
  mostly empty texture - a radar tile of sea - averaged over all its texels took the rocket launcher's sight) under 110,
  and `alike`: the alpha's and the brightness's correlation each at least 0.65 where it can be measured. The right
  pairs scored 0.72..0.99, the wrong ones at most 0.61 on the side that tells them apart - a particle's glow taken for
  a radar icon, the moon for a radio logo, an X for the N, a loading screen for the memory card's, two dark menu
  backgrounds (difference 7.6, the drawing's correlation -0.26: an exception for near-identical colours let it in and
  was taken out). Colour difference alone had ranked wrong pairs above right fonts.
- **Map tiles by structure.** The pack redraws the radar in the PSP's style (black roads with white edges, planes at
  the airport); the colours differ, the roads and coasts do not: the brightness's correlation at 32x32 where both have
  land, 0.80..0.94 for the right tile against at most 0.57 for the next over the 26 dumped radar tiles; a tile needs
  0.7 and 0.2 over the next. Tiles of open sea (no land) are not in the pack: they stay as they were.
- **Where.** The HUD layer's draws, and with the pack any other 2D draw: not the native world's, no depth test. Without
  that condition (`PS2X_HUD_PACK_ANY=1`, diagnosis) the pause menu's titles (Pricedown) and a few of its pieces matched
  too, but 459 textures were read back and compared in one session - the 3D models' among them - for nothing visible:
  the pause menu's text is drawn sharp already and its map is not the radar's tiles.
- **What does not apply.** The pack's loading screens, menu backgrounds and its PSP-only pictures (PSP_Left ...) are
  the PSP version's art, which the PS2's screens are not: nothing of the PS2's matches them, so they stay the PS2's.
- **Found with** `PS2X_HUD_DUMP=<dir>` (each texture the HUD / 2D draws read, once, raw) over play with every weapon:
  132 textures - 26 radar tiles 128x128, the HUD's numbers 128x64, the fonts 256x256, the bars 64x16, the icons 64x64,
  the radar's blip and N 16x16. `PS2X_HUD_PACK_LIMIT` overrides the difference's bound (calibration).

Checked (2560x1440, the user's graphics settings): the numbers (Text/HUD_Numbers), the fonts (Main_Text, Street_Text),
health and armour with their outline, the AK's and fists' icons (Original Icons), the radar's ring, centre and N, a
radar tile (Map_G3, by structure); every wrong candidate of the calibration run rejected.

## Mouse camera (2026-11-15)

The mouse turns the camera itself on foot and behind a vehicle (it was the right stick before: capped at the stick's
top speed, with the camera's easing). Found by memory diffs of TheCamera (0x43BBF0) while the right stick turned it,
and by writes the game keeps (`pokef`, which needed the frame hook below):

| TheCamera + | What |
|---|---|
| 0x260 | the active camera's mode: 4 on foot (GTA III's FOLLOWPED), 18 behind a vehicle (CAM_ON_A_STRING) |
| 0x2F4 / 0x310 | the heading (radians, 0..2 pi; left +) and its target: written together the game keeps it |
| 0x2F8 / 0x300 | the pitch (+ looks down, the camera rises; the stick reaches 1.466; a negative one kept as 2 pi - x) and its negation (the target) |
| 0x308 | FOV (70) |

The fields are inside a CCam: three of them (GTA III's `Cams[3]`) at TheCamera + 0x260 / 0x510 / 0x7C0 (stride
0x2B0), the active one's index a byte at TheCamera + 0x50; offsets above minus 0x260 (mode +0, heading +0x94 / +0xB0,
pitch +0x98 / +0xA0). `vcs::input::GameLayout::direct*` (`vcs_input.cpp`, `directCamera` = TheCamera): in modes 4 / 18
the mouse adds to both of each pair, on frames the game ran; Keyboard > Mouse sensitivity / Car camera sensitivity /
invert apply. The game clamps the pitch itself (on foot it will not look up past ~-0.07). Measured (`VCS_MOUSE_LOG=1`):
the view is at the new heading within a frame (the stick path took ~400 ms to reach a fraction of it).

Then (the user: "it goes back to one position, on foot and in the car, and sometimes looks down while driving"): the
game's own following turns the heading back behind the player / the vehicle on every frame it is not written, and the
pitch target written once stayed. The direct camera now keeps its own heading and pitch (taken from the game when the
mode or the active CCam changes - cutscenes, getting in / out) and writes them on every frame the game runs in modes
4 / 18; the pitch kept in the game's range (on foot it looks up to -0.07, the stick's 1.466 down; in a vehicle
-0.07..1.30). Measured at 60 fps: after a turn, 2.4 s of walking or driving leave the view within 1.5 degrees of where
the mouse put it.

Then (the user: small hitches when the mouse moves fast with the camera far, the car camera shaking and not looking
up, a limit looking up on foot): the game's own following, smoothing, terrain pitch and limits still ran on the
written angles. **The camera's result is replaced** now: `CCamera::Process` (0x28FAC8) takes the active CCam as
TheCamera + 0x220 + ActiveCam * 0x2B0 (ActiveCam the byte at TheCamera + 0x10C - not +0x50 as first assumed, which
only worked while it was 0), calls `CCam::Process` 0x27A380 and right after 0x287020 with the same CCam, then builds
the matrix / frustum from its Front (+0x1C0), Source (+0x1D0, a copy at +0x1E0) and Up (+0x1F0). A hook on 0x287020
(before the original; `LCS_CAMERA_RESULT=0` off) has `vcs::input::directCameraResult` replace those: the point the
game looked at (on its line of sight, as far along as the player / vehicle), the game's distance (its zoom), the
mouse's heading and pitch; the camera stands at a pitch of at least 0.08 so looking up tilts the view instead of
putting the camera under the ground (it did, behind a car). Measured: walking / driving after a turn the view stays
exactly where the mouse put it; it looks up to 26 degrees on foot and in a car.

Then (the user: looking up felt unnatural, the camera shook on bikes at some angles): the camera orbits at the full
pitch now, always looking at the pivot, as the PC games do. Where it would go under the ground (on foot: the ped's
centre - 0.7) it **moves in** towards Toni, down to 0.6. Behind a vehicle, moving in put it inside the car, so it
stays at its distance and stops on the ground (centre - 0.2) looking up. The pivot is the entity's position plus the
game's offset to the point it looked at. That offset and the distance are eased with a 0.2 s time constant (reset on a
new mode / CCam / entity), because the bike camera's lean and bumps came straight through. Limits: up to 0.80 rad on
foot, 0.50 in a vehicle (more and the car leaves the picture). Measured: 40 degrees up on foot with Toni framed from
below; in the car the camera stays outside.

Next round (the user: turning felt like a smooth, trailing move; in a car it should look up like on foot): the offset
across the ground turns with the view, so easing it made the camera trail the mouse. Only the height and the distance
are eased now. Behind a vehicle the camera moves in like on foot (ground: centre - 0.4), but no closer than half its
distance, then it stays on the ground. The vehicle limit is 0.80 like on foot; the car is seen from below, outside it.

Compared with VCS's mouse camera (the user: make it feel like VCS's; a limit at walls and looking up, the car seen
from inside). VCS turns the game's own camera, so the game keeps its collision, and behind a car it goes back behind
the vehicle after Keyboard > Car camera return (`vcs_carcam.cpp`). LCS's direct camera does both now:
- **The return:** in a vehicle, the camera eases back to the nose's heading once the mouse has been still for that
  long, on game time: 0.6 s easing, at most 2 rad/s. The pitch goes back to the one taken from the game when the mode
  started. The camera then keeps following the nose until the mouse turns it. The vehicle's forward is at +0x10 of
  its matrix (checked: after the return the view matched the nose to a degree).
- **The world:** the game's camera, run at the mouse's heading and pitch (they are written into its CCam), stands where
  this one does on open ground. Measured with `VCS_CAMRES_LOG=1`: on a road, the same Source to 0.05. Looking up, it
  stays at its full distance just above the ground (car z - 0.17). Where its view is blocked it rises over the obstacle
  and looks down: behind a car on a grass slope by a tree, the game's Source was 8.7 m above the car, looking 63° down,
  while ours was in the grass. So `directCameraResult` does three things:
  - it lifts this camera to the game's height when the game's is up to 0.5 m higher;
  - from 0.5 to 1.5 m higher it blends into the game's Source / Front, in 0.08 s, and back out in 0.35 s;
  - its distance follows the game's pull-in at once (0.04 s) and eases back out.

  On foot only from level down, because looking up there is deliberately low (moved in towards Toni).
  `LCS_CAMERA_GAME_BLEND=0` turns this off.
- **Vehicle limits:** looking up 0.45 rad, moving in no closer than 3/4 of the distance (closer, the car was seen from
  inside).

The user's play, recorded (a screenshot a second and the camera ten times a second while they played; the user's
report: "se están repitiendo errores"):
- **The bike shake.** On a PCJ-600 looking up at the limit, the game's camera found the ground in its way time after
  time. Its height jumped by up to 2 m between frames, and its Front went from 0.15 up to 0.94 down. The blend
  followed it, so the view's pitch shook by ±3°.
- **The fix.** The blend now runs only from level and down. Behind a vehicle the game camera's height is a floor,
  eased (0.3 s up, 0.6 s down) and never above the pivot + 1.5 m. A lifted camera keeps looking at the point at its
  distance ahead, so it tilts down by just the lift's angle.
- **Checked.** On flat ground it still looks 24° up. On the grass slope it stays above the grass, the car at the
  bottom of the picture: before, it was inside the hill.

Then (the user: on foot and driving the mouse camera "moves too much", it does not keep a fixed place as with a
gamepad). The camera was measured with the mouse still, driving through the park. It had taken the game's look-at point
and distance as they came each frame. Now:
- **Look-at point:** the entity's x / y (the game's look-at point is over the entity: < 0.1 m across, noise), at the
  entity's height eased (0.15 s in a vehicle, 0.08 s on foot) plus the game's height offset (eased over 0.3 s).
- **Distance:** eased over 0.4 s, and quickly (0.05 s) only when the game's camera comes in by a quarter or more.
- **Measured** in the hook, every frame (`VCS_CAMRES_LOG=2`; `LCS_CAMERA_GAME_BLEND=0` for the orbit alone): the car's
  height jumped 0.10 m a frame on the grass, and the camera's distance to it changed by 0.008 m a frame.
- **Measuring from outside:** sampling TheCamera and the entity through the control channel is not a frame-exact pair.
  Its "wobble" of 0.1 m and more is partly that, so judge frame-to-frame motion from the log.
- **What still moves the most:** the blend into the game's camera where its view is blocked. It rises over the
  obstacle like GTA III's camera; the PC games move in instead (to do).

Then (the user: the foot camera is perfect - leave it; the vehicle camera is not free yet, shakes in some positions,
and does not move as smoothly as on foot). Found by turning the vehicle camera 360 degrees at three pitches, standing
and driving, and logging every frame (`camsweep`; `VCS_CAMRES_LOG=2`). The vehicle path still had what the foot path
does not:
- **The floor.** Behind a vehicle the game camera's height was used as a floor. Looking up, the game's camera (asked
  for the same pitch) kept finding the ground in its way, and its height went from -0.2 to +2.9 m, so this one floated
  with it. Now the floor is gone. The game's camera is given at most its own limit looking up (-0.07, in `readPad`),
  so it never fights the ground.
- **Getting in.** The eased height started from a stale value, and the camera dived 30 m under the ground for 5
  frames. A jump of more than 2 m is now taken at once.
- **The return behind the vehicle.** It moved the camera by itself. Keyboard > Car camera return now defaults to
  Never on LCS.
- **Result:** frames with a jolt over the sweep went from 183 to 94. The rest are the test's own pitch steps and the
  eased zoom when the game's distance changes (4.5 to 9.4 m on driving off).

Then (the user: the shake comes when looking up or down and centring the camera again while driving). Found with
`camcenter`, which tilts the view down and back, then up and back, in small mouse steps while driving, logging every
frame:
- **Moving in looking up.** Just past level, moving the camera in so it stays above the ground changed the distance
  by 1-2 m for each mouse step (9.6 <-> 7.2 m). Behind a vehicle it now keeps its distance and stays on the ground.
- **The eased height on a slope.** Driving down a slope, the eased height lagged by up to 2 m, then snapped (a 1.5 m
  jolt). Now it never lags by more than 0.3 m.
- **The blocked test.** It is now the excess of the game camera's pitch over the pitch it was given. The game writes
  the pitch it ended at back into its alpha, so the given one (`max(alpha, -0.07)`) is used. It is 0 on open road,
  even looking up, and 1.11 rad on the blocked grass slope. The old test (its height against this one's) fired looking
  down with nothing in the way, because the two cameras orbit differently there. Unblocked looking down, the game's
  camera looks less steeply than asked (0.5 rad for 1.1): a negative excess, never a blend.
- **The blend behind a vehicle** goes in and out over 0.25 s.
- **Result:** on open road, jolts beyond the mouse's own movement went from 42 to 4, the worst 0.07 m (was 2.4 m). In
  the blocked spot the view is the game's own camera, which moves as with a gamepad. The foot camera is unchanged
  (checked: `camfree` gives the same numbers).

The user's ride, recorded frame by frame (`VCS_CAMRES_LOG=2` under the watch; 7370 frames, mostly on a PCJ-600),
found the shake that was left:
- **The bike camera's own pitch.** On a bike the game's camera looks ~0.3 rad steeper than the pitch it is given,
  with nothing in the way. Its pitch alternates 0.33 <-> 0.38 from frame to frame.
- **The false blocked view.** At the 0.15 rad threshold the camera was half on the game's camera in 70% of the frames,
  and its height went up and down 0.2 m a frame.
- **The fix.** The blocked test starts at 0.6 rad, against an excess eased over 0.15 s: the ride's spikes over 0.6
  lasted 1 - 6 frames, while a blocked view is ~1.1 rad and lasts. When it does blend, the game's camera is taken
  eased (0.12 s, relative to the vehicle).
- **Checked.** The grass slope still goes over to the game's camera. On the `camcenter` run this camera's own jolts are
  9; the rest happen where the view is the game's camera (the car stuck by the tree).

A second recorded ride (6116 frames, a bike, looking up at the limit) showed one more case.
- **What it was.** Every ~1.8 s the game's camera jumped to 7 m above the bike on every other frame, for about a
  second. The excess ran in single-frame spikes to 1.4: 128 runs of 1 frame, at most 3. Their eased average passed
  0.6, so the blend went to 0.4 and the camera bounced 2 - 3 m up each time, then sank back. Height shake otherwise:
  5 frames in the whole ride.
- **The fix.** The excess must stay past 0.6 frame after frame for 0.25 s before anything blends.
- **Checked.** The ride's recorded excess, replayed through the new rule, blends on 0 of its 6116 frames. The grass
  slope still goes over to the game's camera, and the foot camera is unchanged (`camfree`).

**Steep streets** (a third recording; the user: on steep streets the camera goes through the road looking up):
- **The cause.** The ground under the camera was taken as level at the vehicle's height (eased height - 0.4). Driving
  down a street, the road behind the vehicle is higher than that.
- **Measured** from the recording (the slope from the vehicle's track over 10 frames): in 184 of 4763 vehicle frames
  the camera was under the sloped road, up to 1.08 m. That was 6 - 7 m behind the vehicle on slopes of -0.1 to -0.2,
  looking up.
- **The fix.** The vehicle lies on the road, so its slope along its nose (forward at +0x10: rise over run, eased over
  0.2 s, within +-0.8) gives the road's height where the camera stands. Across the road it is taken as level, so a
  bike's lean does not count.
- **Only ever up.** Next the user saw the opposite on streets going up. Behind a vehicle driving up a street, the road
  at the camera is lower, and following it down put the camera into the road that rises between it and the vehicle.
  The slope now only ever raises the ground over the level one (as before the fix, uphill was fine).

Not yet: the game's camera collision
(walls) works on its own camera, not this one - only its distance carries over.

The first version (commit f683527) never ran in play: it read the camera through `GameLayout::theCamera`, which LCS
leaves 0 to keep VCS's camera code off, so it read address 0x260 (mode 0) and the mouse stayed the stick - the user
felt no difference. Its test had passed by the stick path's own turning.

**The frame hook** (`lcs_overrides.cpp` `frameBoundary`, on the limiter 0x2B5400 - VCS's markFrameKick): the rig's
game-thread jobs run there, so the control channel's `poke` / `pokef` / `dump` / `scm` work on LCS now (they never
ran: their jobs waited for VCS's frame kick); the hold while the window is in the background too.

## Mouse aim (2026-11-16)

**First attempt, reverted** (commits f246419 / de7ead8, reverted by 4171abc / dfdd611). LCS still holds GTA Vice City
PC's mouse aim, switched off: CCam::Using3rdPersonMouseCam 0x286F88 and the switch CCamera::m_bUseMouse3rdPerson at
0x3D6C98, cleared every frame by the menu manager at 0x34B160. Switching it on gave VC PC's firing turn and strafing.
- **The user's verdict:** "totally broken": the shots did not match, walking animations broke. Every movement went
  through VC PC's strafe velocity code (FUN_001D5830: `(lock-on cam || mouse cam) && player && state in
  {0,1,8,9,0x10,0x11,0x16,0x29}`), which slides the walk, and LCS has no strafe anims for that.
- **Not used now.** The patch is gone.

**Not portable from VCS.** VCS's free aim is the game's own: R1 + L3, a lock-on to an invisible dummy object.
- **The button.** In LCS the only code that reads the free-aim button (FUN_00172730: L3, or +0x26 in control
  config 4) is a script opcode (in FUN_0024DD28); the pad's L3 (+0x30 of CPad) is otherwise read only by
  GetLookBehindForPed (0x171478).
- **The help text.** HELP42 ("while aiming press L3 for free aim") exists in the GXT.
- **The mechanism.** None of VCS's free-aim functions (start 0x2394D8, update 0x2343E0, end 0x23ADC0) match by code.

**What the aim is built on** (found by decompiling the ped code, then tested one field at a time with
`LCS_AIMTEST`-style probes):
- **CPed's aim fields.** The 64-bit flags at **+0x190**: bit 35 AIM_GUN's, **bit 36 the head's look**, **bit 39
  aiming the gun**, bit 40 restoring it. **+0x34C** is the state (1 idle, 0x10 attack, 0x11 fight, 0x16 AIM_GUN,
  0x2 look entity). **+0x6E4** is the look / aim angle and +0x6F4 its timer. **+0x6E0** is the look target,
  **+0x530** m_pPointGunAt, **+0xC78** the player's aim pitch (+ down). +0x4E0 / +0x4E4 are the heading now /
  wanted (atan2(-x, y) of the forward vector).
- **The functions.** SetLookFlag(angle) 0x1D4D60 writes the angle and bit 39 (it is VC's SetAimFlag(float));
  SetLookFlag(entity) 0x1D4850 sets bit 36; SetAimFlag(entity) 0x1D4E28 writes +0x6E0 / +0x530.
- **What runs them.** The ped update (0x1DA5B8..) runs AimGun **0x1D4F58** on bit 39: PedIK::PointGunInDirection
  (0x270730) at the angle / pitch, or at m_pPointGunAt. It runs RestoreGunPosition 0x1D50E8 on bit 40, and the
  head's Look 0x1D4A08 on bit 36.
- **Why the arm did not rise.** The IK only turns an arm that is in the weapon's aim anim. **CPed::SetPointGunAt
  0x1BA618** blends that anim (0xC9, speed 0, blend 8), sets state 0x16 and bit 35 - **with a null entity too**: the
  pose with no target. Measured: bit 39 + angle alone, or state 0x16 written, left the arm down; SetPointGunAt(ped, 0)
  raised it.

`src/game/lcs_mouseaim.cpp` (Keyboard > **Free aim**, `mouse.aim`, on; keyboard / mouse only; `LCS_MOUSE_AIM=0` off):
- **Entering.** With the right mouse button (R1's binding) held and a gun in hand, on foot, in the follow camera:
  the first frame chains `SetPointGunAt(ped, 0)` as a guest tail call from the frame hook (pc / a0 / a1, returning
  to the limiter's caller; the limiter is skipped that one frame).
- **Each frame.** It writes the camera's heading into +0x6E4 and his heading, the camera's pitch into +0xC78 (sign
  checked: +down), and sets bit 39 / clears bit 40. R1 goes nowhere (no lock-on) and the left stick is centred: he
  stands (no strafe anims).
- **Leaving.** **CPed::ClearPointGunAt 0x1BAA70** (ped) is chained, the game's own way out of a target. It clears the
  look / aim flags (0x1D4930, 0x1D4ED8) and bit 35, and takes state 0x16 or 0x10 to 1 (0x1D3CF0). It also fades out the
  weapon's aim anim: 0xC9, or 0xCA for the weapons with flag 0x20000, blend delta -4. The first version only wrote
  the flags and the state, which left that anim held at speed 0 now and then: Toni frozen in the aim pose after letting
  go, seen by the user with the micro SMG. If the game puts him back to idle (hit, reload), SetPointGunAt is chained
  again after 0.3 s.
- **Shots.** The instant-hit shot (line test 0x2B0BD0, return 0x2A59BC) is tested down the camera's ray through the
  middle of the picture, from the ray's point nearest the gun. The shotgun's pellets (return 0x2A93E4) keep their
  direction, moved onto the ray. The start vector is at 0x7FF00 (EE kernel RAM above the runtime's heap / pools,
  zero in play).
- **The camera and the circle.** The direct camera goes over the right shoulder while aiming: 0.6 m right, 0.12 m
  up, 30% nearer, eased 0.12 s (GameLayout::aimCameraShift). A circle with a dot is drawn in the middle (ImGui).
- **Which weapons.** Fire types 1 (instant hit) and 3 (the flame-thrower). The snipers (types 28 / 29: the target
  button opens their scope, camera mode 7) are left to the game, as are melee (0) and thrown (2) weapons.
- **Weapon info.** `*(*(0x3D9B48) + 4) + type * 0x70`: +0 the fire type, +4 the range, +0x6C flags.

**Checked** (aimlab2: the weapons cheat, each step screenshotted at 1920x1080, the state read back):
- **Aiming.** State 22 with the arm up, the camera at 3.3 m (4.6 not aiming), Toni's heading equal to the camera's
  through three 21.5-degree turns.
- **Pitch.** The arm rises looking up and falls looking down.
- **Movement and fire.** W while aiming leaves him where he stands. Firing shows the impact at the circle. Releasing
  gives state 1, the camera back, and normal walking.
- **Each weapon.** Fists / knuckles / grenades unchanged, the snipers' scope (mode 7) unchanged; the pistol, shotgun,
  SMG, rifle and flame-thrower aim.

**Running with the one-handed guns (2026-11-16, the user's report).** With the pistol and the SMGs he moves while
shooting in the original, but the mouse aim planted him.

Measured in the original (Free aim off, fire held with W, 1 s each): he runs and fires with the **pistol (17), Tec-9
(22) and MAC-10 (24)**: state 0x10 with bit 39, the arm aimed by the IK. Every other gun plants him, including the
.357, the micro SMG, the SMG, the M4, the shotguns, the M60 and the minigun. Those three guns are exactly the ones
with **flag 0x80** in the weapon info's +0x6C, VC's CANAIM_WITHARM. The flags read:

| Weapon | Type | Flags (+0x6C) |
|---|---|---|
| pistol | 17 | 0x680C0 |
| Tec-9 | 22 | 0x280C0 |
| MAC-10 | 24 | 0x280C0 |
| micro SMG / SMG | 23 / 25 | 0x28840 |
| M4 | 26 | 0x28050 |
| AK-47 | 27 | 0x28040 |

What changed for those three guns:

- **The left stick.** It is no longer centred: the game walks him in AIM_GUN (0x16) too.
- **His heading while running.** It is left to the game, which turns him where he runs. The mouse aim keeps writing
  the gun's angle / pitch, so the arm points at the circle as far as it turns.
- **His heading standing.** Standing (the stick reaches the mouse aim through `GameLayout::aimStick`), he turns to
  the camera's heading, eased over 0.08 s.

Two variants were tried:

1. **Heading kept on the camera while moving.** He slid sideways and backwards in the run anim, the look the user
   rejected in the first attempt.
2. **Heading left to the game.** This looks like the original's run and gun, and it is the one kept.

**Checked** (W / A / S / W+fire / D+fire, then stopped, per weapon):

- **Pistol, Tec-9, MAC-10.** About 3.6 m a second in every direction, firing too. The shots count down the camera's
  ray, the impact is at the circle running sideways, and he faces the camera again 0.6 s after stopping.
- **SMG and AK-47.** Planted as before.

**The detonator blocks weapon switching: the original's rule, not a bug.** After a remote grenade is thrown he
holds the detonator (type 34, slot 9). CPlayerPed::ProcessWeaponSwitch **0x34BE08** skips the whole cycle while the
weapon in hand is type 0x22, so the weapon can only be switched again once the grenade is set off. Checked in game:
cycling did nothing until the detonation, then cycled normally. The same happens with the cheat's weapon set.

**Checked** (the release fix): 9 guns (micro SMG, SMG, M4, AK, the shotgun, the combat shotgun, the pistol, the
Tec-9, the flamethrower), each let go 5 ways: plain, while firing, right after firing, while moving, and a 0.08 s
tap. All 45 ended in state 1 with the flags clear, and he walked normally right after (screenshots).

**After getting out of a vehicle (2026-11-16, the user's report: back to the classic aim until he got in and out
again).** "On foot" was the in-vehicle flag (+0x538) off **and** no vehicle at +0x534 - but +0x534 is m_pMyVehicle,
which the game keeps after he gets out (his last vehicle; FindPlayerVehicle 0x107278 reads the flag only). So after
getting out with F the mouse aim never started and the game's own lock-on aim (state 0x16) took the button. Now the flag
alone. Checked: out of a car with F, his last car still at +0x534, the aim active (before: inactive).

**The gun off to the side after getting out (2026-11-17, the user's next reports: an odd pose until the first shot;
then, after a first fix, "it got worse").** With the AK-47, aiming after getting out of a car or off a bike pointed the
gun to his side, the torso twisted and bent over; a shot put it right.

- **What drives it.** CPed::ProcessControl (0x1D6F30, at 0x1DA5B8..) runs, by the flags at +0x190 (GTA III's order:
  35 pointing the gun, 36 looking, 37 keep trying to look, 38 restoring the look, 39 aiming, 40 restoring the gun, 41
  can point the gun): the aim **0x1D4F58** if aiming, else the gun's restore 0x1D50E8; then the look 0x1D4A08 if
  looking, else its restore 0x1D4D10. The aim, with **m_pPointGunAt (+0x530) empty**, calls **PointGunInDirection
  0x270730** (the angle +0x6E4, the pitch +0xC78, CPedIK at +0x310); with an entity there, it points at that entity
  (0x270608). The result is bit 41. Both turn the torso (CPedIK +0xC / +0x10 = ped +0x31C / +0x320) with RotateTorso
  0x26FFD8, 0.14 / 0.087 rad a frame, within the limits at 0x393598 (yaw +-0.873, pitch +-0.785); a target past them
  fails (0).
- **The cause.** Getting out of a vehicle leaves **the vehicle in +0x530**. The aim then pointed at the bike / car
  behind and below him: the torso at both limits (-0.87, 0.79 off a bike, 0.38 off a car) and the gun off to the side.
  A shot sets the attack's own target. Found with the user's recorded play (`watch_aim`: the aim's state 10 times a
  second and screenshots) and a test reading +0x530 = +0x534 (his last vehicle) after getting out.
- **The fix.** The mouse aim drops a target left in +0x530 when it starts and while it aims, as the game clears its
  references: **CEntity::CleanUpOldReference(entity, &ref) 0x2B5CF8** chained, the pointer 0.
- **Also.** The pitch given is kept within the torso's limit (+-0.75): looking down steeply (with no vehicle at all)
  the IK failed the same way. A look left on (bit 36) is ended with the game's **ClearLookFlag 0x1D4930** (ClearAimFlag
  is 0x1D4ED8); SetPointGunAt sets a look of its own only with a target.
- **What was wrong first.** The first fix straightened the torso whenever bit 41 was off. The IK stepped it back toward
  the target past its limit, 0.14 rad a frame, and failed again: the torso shook between straight and the limit. That
  is what the user saw as worse.

Checked: off a PCJ-600 and out of a Kuruma, aiming before any shot: +0x530 empty, bit 41 on, the torso 0 / 0.05 (as
before getting in), the gun at his shoulder on the circle (screenshot). Looking down: the torso still at 0.75, no
shaking.

**Shotgun pellets (2026-11-16, the user's report: "the impact does not go where I aim").** `LCS_SHOT_LOG=1`
logged every line test of the player's shots. The shotguns' pellets (return 0x2A93E4) are always level: the end is
at the gun's height whatever the pitch. The game spreads them in yaw only, about his heading. The shotgun (19) has 3
pellets of 40 m, the combat shotgun and the stubby 5 of 30 m. The mouse aim had moved each pellet's start onto the
camera's ray but kept its level direction, so aiming up or down the impacts missed the circle. Each pellet now keeps
its yaw offset from the camera's heading, turned about the camera's ray: dir = cos(yaw)·F + sin(yaw)·left. Checked
in the log: the middle pellet equals the camera's forward at 3 pitches, and the outer ones keep the spread across the
picture with the same pitch.

**The scopes and the mouse (2026-11-16, the user's report: the sniper / rocket launcher views did not turn).**

Which modes:

- Mode 7 is both snipers, mode 8 the rocket launcher. 34 (M16-style) and 46 (the camera) are taken with them: these
  are the modes the weapon switch 0x34BE08 refuses to change weapons in.
- In CCam::Process (0x27A380, a switch on mode - 1), all of them run **FUN_0027D440**.
- It keeps the view's **heading at +0xF0** and its **pitch at +0xE0** (+ up, kept in -1.562 .. 1.047). It adds the
  sticks' eased speeds (+0xF4 / +0xE4) to them and builds Front / Source from them every frame. +0xD4 / +0xD8 are
  derived afterwards by the shared code.
- Both sticks move it: the left stick on the PS2, the right too.

Why the mouse did nothing:

- The mouse reached it only as the right stick, a speed that hardly moved it (`VCS_MOUSE_LOG`: 200 counts gave 0.008
  rad).
- Writing +0xD4 / +0xD8 was undone the next frame.

`vcs_input.cpp` (the `lcsScopeMode` branch) now writes +0xF0 / +0xE0 from the mouse, slower as the FOV (+0xE8)
zooms in. Checked for both snipers and the rocket launcher: 200 counts right turn the heading 0.5 rad, 150 up raise
the pitch 0.375, and it stays there with no drift.

**Wheel zoom in the scopes (2026-11-16).** In FUN_0027D440, modes 7 (snipers) and 46 (camera) zoom. The FOV at
CCam +0xE8 eases toward a zoom target at **gp - 0x10C = 0x3D6DE4** while no zoom button is held, kept in 15 (3 for the
camera) .. 70. A wheel notch up multiplies the target by 0.8, a notch down by 1.25 (VCS's steps), in
`vcs_input.cpp`'s wheel code. In those modes the wheel no longer changes weapon, as the switch refuses there anyway.
Checked: 5 notches up took 70 to 23, the limit stopped it at 15, 3 down gave 29, and the sniper stayed in hand.

**The wheel bound to the weapon cycle, the radio, the drive-by view (2026-11-17).** The user binds the wheel to L2 / R2
(up previous weapon, down next) and Z / X to L1 / R1, Q / E as L2 / R2's second keys. Their report: in a vehicle the
wheel shook the camera, no side camera for drive-bys, the scope zoomed with Space only.

- **Why.** A bound wheel turned the built-in wheel off (the radio, the scope zoom), and its L2 / R2 press for one frame
  in a vehicle is look left / right - a frame aside, then back. Now a wheel bound to L2 / R2 is that on foot only; in a
  vehicle and in a zooming scope (the last frame's camera mode) it does what the unbound wheel does.
- **The radio.** The vehicle's station: **+0x2A0**, u8, 0..9 the stations, 10 off (11 positions; VCS +0x2BA with 10):
  found by L1 presses in a spawned taxi, 4, 5 ... 10, 0, 1. `input.vehicleRadio`, `radioPositions` - VCS's code: a
  notch down is L1 (next), a notch up puts the station two back and presses L1. Checked: 3 down 1 -> 4, 1 up -> 3.
- **The side views.** L2 / R2 held in a vehicle look left / right (both: behind), and the drive-by fires from them,
  as in GTA III / Vice City - checked with the mouse camera off. With it on, the mouse's car camera rewrote the heading
  and the camera's result every frame, so the side view never showed. `GameLayout::lookAsideButtons` (LCS: L2 | R2):
  held from a key or the gamepad (not a mouse button), the mouse camera leaves the car camera to the game, and takes it
  from the game again when they are let go. Checked with the user's keys: Q / E held gave the side views, shots fired
  (ammo 50 -> 44), the view went back behind the car after.
- (A trap met on the way: tests take the settings from a copy of the user's lcs.ini; an older copy still had Z / X as
  L2 / R2, which made L1 / R1 look like the side buttons for a while.)

**The detonator (2026-11-16).** Enhancements > Combat: **Switch weapons with the detonator**
(`enhance.detonator_switch`, off; `LCS_DETONATOR_SWITCH`), in `src/game/lcs_weapons.cpp`.

- **The game's rule.** The original refuses to cycle while the detonator (type 0x22) is in hand.
- **How the pad is read.** CPad's cycle buttons are L2 (+0x0C, previous, 0x171E40) and R2 (+0x10, next, 0x171EB0),
  "just down" against the last frame's state 0x30 further on. They need the controls enabled (+0x82 zero) and a
  control mode +0x7E of 0..4.
- **The hook.** On ProcessWeaponSwitch 0x34BE08 (ped, pad). With the setting on, the detonator in hand and a cycle
  button just down, it replaces that call by **CPed::SetCurrentWeapon(ped, slot) 0x1E13A0**, a tail call, for the
  next / previous slot that holds a weapon: fists, melee, or ammunition.
- **Afterwards.** The detonator stays in slot 9, so the game's own cycling reaches it again, and firing it sets the
  grenades off.

Checked:
- **On.** Thrown, then next went to fists, katana, the grenades; previous to the katana. Cycling back reached the
  detonator, and firing set them off and gave the remote grenades back.
- **Off.** Blocked, as the original.

## Trainer (2026-11-16)

The trainer key (F5, Back + RB) opens VCS's trainer menu (`src/ui/trainer_menu.cpp`, titled LCS TRAINER, `kLcs`).
`src/game/lcs_trainer.cpp` implements `game/vcs_trainer.h`'s interface for it; `vcs_trainer.cpp` is left out of the
build (CMakeLists.txt), since its script VM addresses and command numbers are VCS's. Pages:

- **Player**: god mode, heal, never wanted, set wanted level, add money, infinite sprint, police / everyone ignore you.
- **Weapons**: any weapon (put in his hand on foot), the cheats' three sets, unlimited ammo, no reload, fast reload,
  remove all.
- **Vehicles**: spawn any car, bike or boat (75; in it with Get in spawned vehicles), vehicle invincible, repair, flip
  upright, boost, random paint.
- **Teleport**: the 24 districts, save / return to a position. **Camera**: Hide HUD only. **World**: time of day,
  freeze time, weather, game speed.
- Not on LCS: missions, the free camera (VCS's modules).

Two ways in, both from the frame boundary and only while the game's clock runs: the weapons (and the repair's engine
and tyres) are the cheats' own guest calls, chained (below); everything else is a short script on the game's script VM
("The script VM" below), as VCS's trainer does. Control channel (tests): `trainer state | toggle <god|wanted|ammo|
reload|sprint|fastreload|police|ignore|vehicle|warp|freeze|hidehud> 0|1 | list vehicles|weapons|landmarks|weathers |
notices | heal | money N | setwanted N | weapon T | loadout N | noweapons | spawn MODEL | repair | flip | boost | paint
| teleport N | savepos | returnpos | time H | weather W | speed PERCENT | scm <hex>` (`ammo` / `noreload 0|1` still
work).

**How the game gives weapons.** This was found with a write watch (`watch` on the player's weapon slots, ped+0x594,
with `VCS_WATCH_STACK=1`) while entering the weapons cheat. The writers were CWeapon::Init 0x2A2650, called from
0x1E1314 inside **CPed::GiveWeapon 0x1E1180** (ped, type, ammo, 1), called from the cheat **FUN_0016C838**. The
other two sets are FUN_0016CA70 and FUN_0016CC80. Each cheat does the following:

1. Shows its text (0x23EF60).
2. Calls **CStreaming::RequestModel 0x18B400** (model, 1) for every weapon model.
3. Calls **LoadAllRequestedModels 0x18F648** (0).
4. Calls FindPlayerPed 0x107368, then GiveWeapon for each weapon.
5. Calls **SetModelIsDeletable 0x18FC38** for each model.
6. Adds 1000 to 0x3D9CE0 and sets gp-0x6600 to 1. These look like the cheat counters. The trainer leaves both
   alone.

Related functions:

- **CPed::SetCurrentWeapon(ped, slot) 0x1E13A0**: removes the old weapon model (0x1E0F50) and sets +0x6B8. For the
  player it also sets the player info's +0xB64. Then it adds the new model (0x1E0E10).
- **SetCurrentWeapon(ped, type) 0x1E1470** goes through the slot lookup 0x1E1380 (the weapon info's +0x68).
- **CPed::ClearWeapons 0x1E16C8** shuts down the 10 slots (CWeapon::Shutdown 0x2A2728), then selects fists.

**Weapon table.** The table is at `*(*(0x3D9B48)+4) + type*0x70` and has 37 entries (0..36). Its fields:

| Offset | Field |
|---|---|
| +0 | fire type: 0 melee, 1 instant hit, 2 projectile, 3 area, 4 camera |
| +4 | range |
| +0x10 | clip size |
| +0x14 | damage |
| +0x60, +0x64 | models (-1 for none) |
| +0x68 | slot |

Two weapons have a second model: the remote grenades (270, plus the detonator 291) and the minigun (290 and 294).

Weapon types:

| Type | Weapon |
|---|---|
| 1-11 | knuckles, screwdriver, golf club, nightstick, knife, bat, hammer, cleaver, machete, katana, chainsaw |
| 12-15 | grenades, remote grenades, tear gas, molotovs |
| 16 | the rocket (the projectile) |
| 17-18 | pistol, .357 |
| 19-21 | shotgun, combat shotgun, stubby shotgun |
| 22-25 | Tec-9, micro SMG, MAC-10, SMG |
| 26-27 | M4, AK-47 |
| 28-29 | sniper, laser sniper |
| 30-33 | rocket launcher, flamethrower, M60, minigun |
| 34 | detonator |
| 35 | the helicopter's gun |
| 36 | camera |

The names were checked against the HUD icon each weapon shows.

**Weapon slot.** Each slot is 0x1C bytes at ped+0x594: type, state, +8 ammo in the clip, +0xC ammo in total.

**Calls from the frame boundary.** The trainer queues the cheat's own sequence. Each call is chained from the
limiter hook (`frameBoundary`, `lcs_overrides.cpp`) with its `$ra` set to the limiter's entry 0x2B5400. When it
returns, the hook runs again and recognises the return by `$ra` == 0x2B5400. It then puts the caller's `$ra` / a0
back and chains the next call or runs the limiter. A whole set is therefore given in one frame, as the cheat does.

The first version skipped the limiter for the frame instead, as the mouse aim had done. Six skipped limiters in a row
killed the game thread: `[guest-branch:missing-target] JALR source=0x2c8f6c target=0x0`. That is FUN_002C8E40
calling a virtual function on each entry of the list at 0x60E408, and one entry had a null vtable entry. The mouse aim
now returns into the limiter the same way. The trainer only acts while the game clock (0x3D9B70) runs. In the pause
menu it answers "Only while you play".

**Checked** (scratch tests: the trainer command, then the slots read back, screenshots at 1920x1080):

- **Each weapon on its own.** All 33 weapons were given one by one. Each lands in its slot with its ammunition and
  goes into his hand, with the right icon on the HUD.
- **Sets and removal.** The three sets match the cheats' slot for slot. Remove all weapons leaves every slot empty.
- **Ammo toggles.** Unlimited ammo keeps the total at 9999. No reload keeps the pistol's clip at 17 through 3 s of
  firing.
- **The menu.** F5 → Weapons → Give weapon gives the selected weapon.
- **Pause and car.** Nothing is given in the pause menu. In a car the weapon is given but not put in his hand.
- **The mouse aim afterwards.** It still enters, fires through the circle and releases.

### The script VM (2026-11-16)

Found from the script Init 0x109A88 (`tools/xmatch.py` from VCS's 0x2551D0: the only VM function that matched), its
callers, and the handler blocks decompiled (`tools/eedecomp.py`). The VM is GTA3's, not VCS's:

| What | Where |
|---|---|
| ScriptSpace (MAIN.SCM); an IP is an offset from it | `*(0x3CF0F4)` (gp - 0x7DFC) |
| Active / idle `CRunningScript` lists (+0 next, +4 prev, insert at the head) | 0x3D9C00 / 0x3D9C04 (remove 0x10A1B8, add 0x10A1F0: VCS's code) |
| Script id counter | 0x3CF100 |
| Pool | 0x64B110..0x65BF10, 0x21C bytes each |
| StartNewScript(ip) | 0x10A210: idle -> id, Init, IP, active list, +0x20C = 1 |
| CTheScripts::Process / one script / one command | 0x10A288 / 0x10A678 / 0x10A7A8 |
| CollectParameters(script, &ip, n, out 0x3DF320) | 0x1094C8 |
| Variable address | 0x1098D8 |
| A mission is running (u8) | 0x3D9C29 (cleared by TERMINATE_THIS_SCRIPT on the mission's script) |

`CRunningScript` (from Init): +0 next, +4 prev, +8 id, +0x10 name (8 chars, `"id%02i"`), +0x18 IP, +0x1C 16-word gosub
stack, +0x5C (u16) its depth, +0x60 0x6A-word locals, +0x200 / +0x204 the timers (variables 0x0A / 0x0B), +0x208 the
local frame base, +0x20C active, +0x20D the condition result, +0x20E launched as a mission, +0x210 wake time, +0x214
(u16) the IF's count, +0x216 NOT, +0x217 death / arrest check (Init sets 1; SET_DEATHARREST_STATE 0x116), +0x219 the
mission script.

**Bytecode.** Opcode u16, bit 15 the NOT. Arguments: a type byte - 0 end, 1 / 2 zero, 3 / 4 / 5 a float of one / two
/ three high bytes, 6 int32, 7 int8, 8 int16, 9 float32 - or a variable: 0x0C + n the local n (0x0C..0x6B), 0x6C..0xCB
a local array, 0xCC..0xE5 a global ((b - 0xCC) * 256 + the next byte), 0xE6.. a global array. VCS's assembler works as
it is.

**Commands.** ProcessOneCommand dispatches by hundreds to 17 functions (0..99 a jump table at 0x3A6320; 100..199
0x10DA48; 200..304 0x10FF30; ... 1600..1699 0x273080). The numbers are GTA3's up to ~0xC6 (0x4E TERMINATE, 0xA5
CREATE_CAR, 0xC0 SET_TIME_OF_DAY), then shifted by +5 (IF is 0xDB, ADD_SCORE 0x10E) and by +4 later on. Each one the
trainer uses was read in its handler:

| Opcode | Command | The handler |
|---|---|---|
| 0x004E | TERMINATE_THIS_SCRIPT | active -> idle, +0x20C = 0 |
| 0x00A1 | SET_CHAR_COORDINATES (char, x, y, z) | z <= -100: the ground; the vehicle if he drives |
| 0x00A5 | CREATE_CAR (model, x, y, z, out) | CAutomobile 0x780 (0x1221D0), CBike 0x650 (0x154A88), CBoat 0x590 (0x17CED0) |
| 0x00AB | SET_CAR_COORDINATES | |
| 0x00C0 | SET_TIME_OF_DAY (h, m) | 0x1F87E0 |
| 0x010E | ADD_SCORE (player, n) | Players[i] +0xBC |
| 0x0112 / 0x0115 | ALTER_WANTED_LEVEL (player, n) / CLEAR_WANTED_LEVEL | 0x34D770 -> CWanted::SetWantedLevel 0x2F4C48 |
| 0x0162 | SET_TIME_SCALE | 0x3D9CE4 |
| 0x017A | SET_CAR_HEADING (car, degrees) | 0x2EB388: SetRotateZ 0x26BBC8 (the whole matrix, up = z), position kept |
| 0x01BB / 0x01BC | FORCE_WEATHER_NOW / RELEASE_WEATHER | 0x325728 / 0x325748 |
| 0x01C8 | MARK_CAR_AS_NO_LONGER_NEEDED | |
| 0x01FC / 0x03C4 | SET_POLICE_IGNORE_PLAYER / SET_EVERYONE_IGNORE_PLAYER | ped +0x808 bits 48 / 49 |
| 0x0229 | SET_CAR_HEALTH | +0x268 |
| 0x022E | CHANGE_CAR_COLOUR | +0x1F0 / +0x1F1 |
| 0x024C / 0x024E | REQUEST_MODEL / MARK_MODEL_AS_NO_LONGER_NEEDED | |
| 0x02B0 / 0x02B1 | SET_CHAR_PROOFS / SET_CAR_PROOFS (5 flags) | +0x40 bits 58, 59, 50, 60, 61 |
| 0x02F0 | RESTORE_CAMERA_JUMPCUT | 0x292CD8 (TheCamera, 0) |
| 0x0335 / 0x0336 | SET_PLAYER_NEVER_GETS_TIRED / SET_PLAYER_FAST_RELOAD | Players[i] +0x16C / +0x170 |
| 0x0367 | WARP_CHAR_FROM_CAR_TO_COORD | |
| 0x036F | WARP_CHAR_INTO_CAR (char, car) | 0x1F0078 |
| 0x03D0 | LOAD_SCENE (x, y, z) | |
| 0x03F2 | SET_UPSIDEDOWN_CAR_NOT_DAMAGED | +0x678 bit 48 |
| 0x04BF | SET_CAR_FORWARD_SPEED | move speed +0x70 (n / 60) |

**Handles.** The player commands take the player's index (0); the char and car commands a pool handle, index << 8 |
the pool's flags byte (CPool::GetAt 0x112068 / 0x111FF8). The trainer computes them on the host: peds `*(0x3D5AE8)`
{entries, flags} 0xCD0 each (the player is index 0, handle 1), vehicles `*(0x3D5AEC)` 0x780 each.

**The trainer's scripts** are VCS's method (docs/vcs-reference/game-internals.md 11d): an idle script, Init as the
game's (zeroed; the death / arrest check off), the code in its own locals 16.. (+0xA0, 360 bytes; locals 0..15 free
for outputs), the IP pointing there (an offset below ScriptSpace: 32-bit wrap-around), at the head of the active list.
The game runs it in its next frame; it ends with TERMINATE_THIS_SCRIPT. One at a time, so an action sees what the one
before did. A script still waiting when the clock stops (a pause) is taken back out; a model is requested and polled
(the streaming info `*(0x3D0CC8)`, 0x14 each, +0xC 1 = loaded) before the script that uses it.

### What else the trainer needed (2026-11-16)

- **The cheats** (FUN_0016C838.., decompiled as a block): health (0x16D0D8: health = the player's most, Players[i]
  +0x178 byte; a car's health 1000, then its damage manager +0x350: engine 0x32FCB8 (dm, 0), wheels 0x32FC50 (dm, i,
  0)), armour (0x16DBF0: +0x4BC = the +0x179 byte), $250,000 (0x16DB40: Players[i] +0xBC), wanted up / down (0x16DC90 /
  0x16DD40: CWanted at ped +0x7F0, its level at +0x810), the weathers (0x16DDD0..: 0x325728 with 0, 4, 1, 2, 3), game
  speed up / down (0x16D9D8 / 0x16DA88: the time scale 0x3D9CE4, 0.25..4), a vehicle by model (0x16D228(model), at the
  nearest car path node: the Rhino 0xA2 0x16D4E8, the Trashmaster 0x8B 0x16D508). Each cheat also shows its text, adds
  1000 to 0x3D9CE0 and sets gp - 0x6600 (counters, presumably): the trainer does not call them.
- **The clock**: hours 0x3D9B74, minutes 0x3D9B80 (GET_TIME_OF_DAY's).
- **Vehicle models** (CModelInfo::ms_modelInfoPtrs `*(0x3D8720)`, count 0x3D9BA0): +0x10 type (6 = vehicle), +0x2E the
  game name (a text key: SPIDER, LANDSTK), +0x38 the class (0 car, 1 boat, 2 train, 3 helicopter, 4 plane, 5 bike, 6
  the ferry; the checks 0x31F948 / 0x31FA68). Vehicles are 130..216; 211..216 (RC planes, helicopters) have no text and
  are left out: 75 to spawn.
- **The text** (`CText` at `*(0x3D89B8)`: +0 entries {u32 text, char key[8]} sorted, +4 u16 count; CText::Get 0x332E68
  -> 0x332058, binary search 0x332100) is in the language the player chose (the user's: Spanish), and so are the names.
  Accented letters are Latin-1 less 0x40 (0xB3 o acute, 0xBA u acute, 0xB1 n tilde, 0x81 A acute); 0xC0..0xFF are the
  font's own symbols.
- **The zones** (CTheZones `*(0x3D7750)`: +8 the districts (u16 count +0x14), +0xC the info zones (+0x16), +0x1C the map
  zones (+0x68); 0x38 bytes each: the text key, then the box min / max). Find by label 0x2D37A0.
- **The path nodes** (`*(*(0x3D0314))`, 0x14 each: x / y / z int16 * 1/8 at +4; ThePaths +0x14 the count 5519, +0x18 the
  car nodes 2196, first). The teleports are each district's car node nearest its centre, inside it and out of tunnels,
  at the node's height + 1 m: the box centres had put him on a Torrington roof, in the sea at Portland Beach and Rockford
  and in mid-air at Saint Mark's.
- **The wanted level's cap**: SetWantedLevel caps at CWanted::MaximumWantedLevel 0x3D829C (the chaos at 0x3D82A0). In
  the first mission (Home Sweet Home) even the level written straight into CWanted reads 0 a frame later: something
  clears it every frame. The trainer says so ("This mission keeps the wanted level down") instead of a level that is not
  there.

**Checked** (scratch tests from a new game, each action read back in memory; screenshots at 1280x720):

- **Player.** Money +10,000. Heal from 30 to 100 / 100. God mode: the five proof bits set, health written to 10 back at
  100, off clears them. Never wanted refuses a level. Sprint, fast reload, police / everyone ignore: their fields set and
  cleared.
- **World.** 22:00 set; frozen for 5 s at 22:00; rain (screenshot); game speed 50 % and back.
- **Vehicles.** The Banshee spawned with him in it; repair from 300 to 1000; paint (new colours); boost (moving); flip
  after a real roll-over (up 0.93 -> 1.0); vehicle invincible (proofs, health held); from the car onto a PCJ-600, from
  the bike onto a Faggio, then a Kuruma, each with him in it; a Predator on foot.
- **The first car-to-bike change failed**: the old car, still at boost speed and driverless, ran into the new bike and
  knocked him off. The old vehicle is stopped now before the change.
- **Teleports**: all 24 on a street (contact sheet), save / return.
- **The menu**: its pages, the vehicle list with the game's names (accents right), Hide HUD.
- **Weapons as before**: set 2 slot for slot, the M4 in hand, no reload holding the clip at 30.

### The user's first trainer report (2026-11-16)

- **Repair did nothing visible.** It set the health (and the health cheat's engine / tyres), so the dents, the loose
  panels and the smoke stayed. It now repairs as the Pay 'n' Spray (FUN_00195238, found from the stores of 1000.0 into
  a vehicle's +0x268): health 1000, the fire timer cleared (+0x6E0 a car, +0x5D8 a bike), then **CAutomobile::Fix
  0x138648** / **CBike::Fix 0x1619F0** (the damage manager reset 0x32FB68, every component frame put back, the wheels).
  Checked: a car wrecked with 0x5F7 (doors, panels, engine broken: its damage manager 00fa0200 0000 0303030303..)
  came back to a new car's bytes and 1000. A door left swinging stays open until the car moves, as after the Pay 'n'
  Spray. (0x5F7 is the opposite of a repair: health 0, doors and panels damaged.)
- **Freeze time changed the weather one after another.** It set the time of day again every 10 frames; the minutes went
  back each time, and CWeather::Update (0x324C50) takes the hour's fraction going back for a new hour: the next
  weather of its list each time. CClock::Update (0x1F88C0) adds a minute when the game clock is a game minute past its
  last tick (**0x3D9DD4**; SetGameClock 0x1F87E0 sets it to now): the trainer now keeps that tick at the game clock every
  frame. Checked: 20 s frozen at 12:16, the weather unchanged; the clock ran on when unfrozen.
- **The camera behind a vehicle looked down with the trainer.** Two causes:
  - With a menu open (F2 / F5) the mouse camera turned itself off (`on = mouseCamera && !blocked`), so the game's own
    camera took over behind the vehicle for that while, and the mouse camera took its pitch back on closing. It now
    stays the camera, held still (the mouse motion dropped while blocked). Checked: the pitch the same before, during
    and after 3 s of the menu.
  - After the trainer's warp into a vehicle the view tipped over it to look straight down within ~1.5 s (also with no
    menu). The game eases its car camera's pitch toward a target of its own with a speed, **CCam +0xE4** (the
    heading's +0xF4); the mouse camera wrote the pitch every frame but not the speed, and where the game's target is
    not that pitch (after a warp: there is no getting in to settle its camera) the speed built up under it: +0xE4 0 ->
    -0.027, the pitch out 0.116 -> 1.1. With the mouse camera off the game's camera stayed level, and a normal entry
    never moved the speed. The speed is now zeroed with the pitch (`GameLayout::directAlphaSpeed`). Ruled out on the
    way: the camera's target (TheCamera +0x494 is the car either way), RESTORE_CAMERA_JUMPCUT (in the warp's script and
    a frame later), WARP_PLAYER_INTO_CAR (0x36E) instead of the char's, the car's AI mode (+0x198) and a ped flag.
    Checked: a warp on an open street (Chinatown) and the three spawn variants all level (front z -0.113..-0.116).

## Foot camera and walls (2026-11-16)

The user saw the foot camera inside walls / a column.

**What was found** (`VCS_CAMRES_LOG=2`, a courtyard at 1140, -242):
- **The game's own camera** pulls in at walls (its distance falls from 4.6 to 1.2 m). At a wall it also turns along
  it: its view was 15 degrees off the heading given.
- **The mouse camera** took only that distance. Standing on its own heading, lower and with the shoulder offset, it
  could end up past a wall.

**The fix** (`src/game/lcs_camwall.cpp`, `GameLayout::footCameraClear`):
- Once a frame the frame boundary chains **CWorld::ProcessLineOfSight 0x174538** (the weapons' 0x2B0BD0 is a jump to
  it), from the pivot over Toni to the camera's final place, 0.3 m past it. The test covers buildings and objects,
  with see-through surfaces included.
- The camera stands where the line is clear: in at once, out again eased, and never nearer the pivot than 0.9 m.
- The arguments were read from its calls (the instant-hit shot 0x2A59B4; CCamera's post-process 0x287020):

  | Register / slot | Argument |
  |---|---|
  | a0, a1 | from, to |
  | a2 | CColPoint (the point first) |
  | a3 | the entity hit (out) |
  | t0..t3 | buildings, vehicles, peds, objects |
  | sp+0, +8, +16, +24 (8-byte slots) | dummies, ignore see-through, ignore some objects, ignore shoot-through |
  | v0 | the hit |

- The chained call's stack arguments sit below a lowered sp (0x40), put back when it returns. The frame hook's
  `Chain` keeps sp, and `chainMore` runs the line test once a frame, then the trainer's calls. The vectors are at
  0x7FE00..
- `LCS_FOOT_WALL=0` turns it off, `LCS_FOOT_WALL_LOG=1` logs each hit. `LCS_CAMWALL_PROBE=1` fires lines 2 m out from
  the camera in ten directions instead (what is around it).

**What was tried before this:**
- **A 1 m margin** (for GTA's ~0.9 m near plane) put the camera in his neck in tight spots.
- **Moving over to the game's camera** when it pulled in: other cases still came from the camera's own height.

**Checked:**
- **A wall right behind him** (case A). Before, the camera stood on the wall's plane; now it is 0.3 m in front, with
  the same view.
- **Aiming at a wall.** Fine.

**A limit of the map, not of the camera.** In that courtyard one wall's visible geometry stands out past its
collision. The probes found nothing within 2 m but the far wall, and the game's own camera shows the same blur when it
comes near it, so no line test sees it. The camera can still show it close up there.

## Button prompts (2026-11-17)

The user: the menu's and the help text's buttons are the PS2's, and blurry. Enhancements > **Button prompts** (VCS's
setting, `enhance.prompts`, Original by default): the keys / mouse buttons bound, or a gamepad's buttons
(`src/game/lcs_prompts.cpp`, the atlas from `vcs_prompts` / `vcs_glyph_art`).

- **How LCS draws its buttons.** The expander of `~k~` (**0x2F8F58**, called by **0x2F91A0** = VCS FUN_003aff20 by
  xmatch; VCS's FUN_003afd78 itself is not in LCS) copies `C<configuration><ACTION>` (configuration: CPad 0 +0x7E) and
  turns ~T~ ~S~ ~X~ ~O~ into the characters 0xE0..0xE3, and ~L~ ~N~ ~R~ into colour codes. The font atlas (`font1`,
  256 x 256, a 16 x 16 grid of 16-texel cells from ' ') holds the buttons in its **last row: △ □ ✕ ○ ◀ ▼ ▶** (columns
  0..6). The pause menu's help bar draws textures `btn_cross_PSP`, `btn_triangle_PSP`, `btn_square_PSP`, `btn_L_PSP`,
  `btn_R_PSP`, `btn_up/down/left/right_PSP` (16 x 16; RwTextures of the frontend's dictionary, only while the menu is
  up: name at +0x10, raster at +0, 0x54 apart; the name table at 0x3C9DD8).
- **Known by content.** LCS names no texture upload by raster (VCS's world renderer does), so the renderer tags textures
  by a hash of their decoded pixels (`gsHwSetTextureTagger`: FNV-1a 64 over w, h and the RGBA words) - the font had 4
  texture keys in one session, one hash. The hashes came from `PS2X_HUD_DUMP` dumps of this disc (SLUS-21423).
  `gsHwSetTextureGlyphClassifier` gets each textured 2D primitive with its tag and texture coordinates: a font quad
  whose centre is in the last row's cells 0..6, or a tagged picture, is drawn from the glyph atlas.
- **Context.** The menu while the frontend's btn textures are in memory (a helper thread scans for their names), a
  vehicle while the player is in one (Cross is the accelerate key, Square the brake, R1 the handbrake), else on foot.
- **Sizes.** A keycap keeps its shape at the glyph's height: in text it grows into the spaces beside it (up to 3 x), on
  the help bar up to 1.6 x away from its words (left half to the left, right half to the right), smaller past that
  (`GSHwGlyphHit::dy`, added for it). First try grew every picture left: "Objetivo - Sí [SPACE]" covered "Sí".
- Checked (1080p, keyboard set): "Pulsa [W] para acelerar" in the car, the help bar's [Esc] Atrás, [Enter]
  Seleccionar, arrow keys, [Space], [Z] and the mouse for Zoom on the map page.

## The pause menu with the mouse (2026-11-17)

`src/game/lcs_frontend.cpp`, in VCS's menu module's place (`vcs::frontend::setDelegate`; vcs_input calls it). The
state, found by RAM dumps across pages and before / during / after the menu:

- **0x634610**: the page (1 Map, 2 Brief, 3 Game, 4 Stats; 5 Controls, 6 Audio, 7 Display; it keeps its value after
  the menu closes). **0x634BC4**: 0 on the tab bar, 1 inside a page. **0x3D9CB4**: 1 while the menu is up.
- Enter = Cross; Escape = Triangle in a page, Start on the tab bar (both Start and Triangle close the menu there).
- The mouse (the cursor over the menu's 4:3 area, as VCS's): a tab hovered gets the focus, clicked - focused already -
  is entered (D-pad steps toward it, one pulse of 60 ms with 60 ms gaps); a click below the tab bar is Cross; the right
  button goes back; the wheel moves a page's selection, zooms the map (R1 / L1). The tabs' boxes are measured on the
  Spanish game. `menustate`, `menumouse X Y [left|right|wheelup|wheeldown]` for tests; `LCS_MENU_MOUSE=0` keys only.

## The pause menu's own table: screens, rows, actions (2026-11-17)

Found to see whether settings can live in the game's own menu (the user's question; the F2 menu or the launcher's Game
sheet otherwise). The menu is the series' classic frontend (GTA III / VC's CMenuManager), not VCS's item objects
(vcs-reference game-internals 6m): a static table of screens in the executable's data, rows with an action code each.

- **The screen table at 0x393A30** (found: the GXT keys `FEH_DIS`, `FED_BRI`... searched in the ELF, then the EE code
  that forms addresses in that range - lui / addiu pairs, a linear scan - all in 0x339A5C..0x342EF0): **0x116 bytes a
  screen** - its title key (8 bytes), then **15 rows of 0x12 bytes**: u16 action, char key[8] (GXT), u8 0, u8 the
  screen's own index, u16 x, u16 y (PSP units, 480 x 272), u16 alignment (2: the label right-aligned at x with the
  value at x + 12; 3: centred, no value). Screens: 0 STA, 1 LOA (Game), 2 BRI, 3 CON, 4 AUD, 5 DIS, 6 LAN, then the map
  and the save / load / memory card screens.
- **Display (screen 5)**: Brightness 0x0D (y 40), Subtitles 0x0B (60), Widescreen 0x0C (80), Radar mode 0x28 (100),
  HUD mode 0x29 (120), Screen position 0x1D (140, align 3) - six of fifteen rows; the help bar starts near y 200, so
  three more rows fit (160, 180, 200). **Audio (4)**: Music 0x0E (40), SFX 0x0F (60), radio station 0x11 (100) - y 80 is
  free, the station logos fill the rest.
- **Drawing, `FUN_00339fe0`** (DrawStandardMenus): each row's label is `FUN_00332e68(text table, row key)` (CText::Get);
  its value comes from a switch on (action - 5) that picks a GXT key from the option's global (Subtitles `gp+0x1c98`,
  Widescreen `gp+0x1c9a`, Radar mode `0x3d08f8`...) and draws it at the row's x + 12. An action the switch doesn't know
  draws its label only. Brightness (0x0D), Music (0x0E), SFX (0x0F) draw bars further down (`sVar3 == 0xd/0xe/0xf`).
- **Input, `FUN_00340b80`** (ProcessUserInput): the manager (`piVar16`) +0x588 the screen, **+0x58C the selected
  row**; up / down skip rows whose action is **0 or 0x19** (labels); left / right / Cross switch on the action - an
  unknown action does nothing.
- So a row of our own is possible: a free slot of a screen's table (guest RAM, written at boot), its text through the
  GXT lookup, its left / right handled by `lcs_frontend` (it knows the screen and row from the manager). Its value has
  no drawing of its own: the label would carry it, or a value row (action 0, a label the cursor skips) beside it.

## Our settings in the pause menu (2026-11-17)

`src/game/lcs_options.cpp`, from the table above (the user's wish: the F2 menu off, what matters in play in the game's
own menu, the rest in the launcher's Game sheet):

- **Rows**: Display becomes Brightness, Subtitles, **Aspect ratio** (in the PS2's Widescreen row's place - the port
  keeps that option off), Radar mode, HUD mode, **Widescreen HUD**, **Frame rate cap**, Screen position (y 40..180);
  Audio gets **Master volume** in its gap at y 80 (Music, SFX, ours, the radio station). Written into the table at the
  first frame kick, and again whenever the table shows the original rows. RAM only: nothing reaches a save.
- **Text**: our rows are 0x0B (Subtitles) rows whose keys are ours (`LCSASP`, `LCSHUD`, `LCSFPS`, `LCSVOL`):
  CText::Get (0x332E68, hooked) gives their labels, and the Get right after one of them (the Subtitles value's keys,
  0x3D8D50 / 0x3D8DA8) gives the setting's value. Written into 0x7C000 (EE kernel RAM no one uses) in the font's code
  page: capitals, Latin-1's accented capitals 0x40 lower (Á 0x81, Í 0x8D - read from the disc's GXT, whose
  ENGLISH.GXT holds the Spanish text on the user's disc). The language is the menus' (`ui.language`).
- **Input**: the menu's input (0x340B80) is hooked for its manager (a0); on one of our rows `filterPad` (the frontend
  delegate's, after the menu's own buttons) takes left / right / Cross and the left stick's x away from the game -
  they would turn the subtitles - and steps the setting (choices wrap, the volume by 10 between 0 and 100); the
  change is applied on the main thread (`settings::set`, as the F2 menu did).
- Checked (hidden runs, Spanish and English): each row steps both ways, the subtitles stay as they were, the station
  row still works below ours. A setting fixed by the environment (the test tools pin `VCS_FPS=30`) does not change.
- **F2** is off by default (`ui.f2_menu`, Advanced > Menus; the launcher turns it on); `LCS_MENU_ROWS=0` leaves the
  game's menu as it was.

## Pending (the user's checklist, 2026-11-17)

In the order agreed with the user (most likely to break play first). VCS has each of these; on LCS none is done.

- [ ] **Above 30 fps**: what counts frames instead of time - particles and effects (`vcs_particle_timing`,
  `vcs_effects_timing`), mission bars and timers (`vcs_script_timing`), physics / music timing, the scripted cutscenes
  stuck on "Loading..." at 60 fps (`vcs_script_fixes`). Expected to break missions as the user goes on.
- [x] **The pause menu with the mouse and keyboard** (2026-11-17, `lcs_frontend`): Enter / Escape, tabs by click,
  the wheel and the buttons in pages, the map's zoom. Still to do: hovering a page's options, sliders by dragging,
  the map dragged.
- [x] **Button prompts by device** (2026-11-17, `lcs_prompts`): the font's glyphs and the help bar's pictures. Still
  to do: words in the help text ("joystick analógico izquierdo") are the game's.
- [ ] **Sound**: still the emulated IOP, never checked by ear; VCS's native sound driver and the music / effects levels
  as settings (`vcs_sound`, `vcs_volume`).
- [ ] **World options**: traffic / pedestrian density, draw distance as settings (`vcs_world_detail`).
- [ ] **The trainer**: missions (story and side jobs), the free camera.
- [ ] **Saves**: save / load in different places, checked.

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
- 2026-11-16: the trainer (F5): weapons - any weapon, the cheats' sets, remove, unlimited ammo, no reload
  ("Trainer").
- 2026-11-16: the trainer's other pages (player, vehicles, teleports, world) through the game's script VM, found
  for it ("The script VM").
