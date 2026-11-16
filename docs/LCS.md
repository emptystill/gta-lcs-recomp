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

The trainer key (F5, Back + RB) opens VCS's trainer menu (`src/ui/trainer_menu.cpp`, titled LCS TRAINER, `kLcs`: the
Weapons page only so far). `src/game/lcs_trainer.cpp` implements `game/vcs_trainer.h`'s interface for it;
`vcs_trainer.cpp` is left out of the build (CMakeLists.txt), since it works through VCS's script VM addresses and
commands. What it has: give any weapon (put in his hand on foot), the cheats' three weapon sets, remove all weapons,
unlimited ammo, no reload. The other actions answer "Not available in Liberty City Stories yet". Control channel:
`trainer weapon T | loadout N | noweapons | ammo 0|1 | noreload 0|1 | state`.

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
