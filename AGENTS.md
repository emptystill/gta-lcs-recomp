# AGENTS.md - working on the GTA: Liberty City Stories native port

Read this first. It is the contract for anyone (human or AI agent) changing this repository. The
detailed knowledge lives in `docs/`:

| Doc | What's in it |
|---|---|
| [docs/LCS.md](docs/LCS.md) | **Liberty City Stories**: disc, executable, status, how it was brought up (each fix and how it was found), open issues, the `LCS_*` switches, the log. Where LCS knowledge goes |
| [docs/DECISIONS.md](docs/DECISIONS.md) | This project's decisions (the VCS port's are in vcs-reference) |
| [docs/PS2RECOMP_PATCHES.md](docs/PS2RECOMP_PATCHES.md) | Every change to the vendored PS2Recomp runtime, with the reason |
| [docs/LINUX.md](docs/LINUX.md), [docs/MACOS.md](docs/MACOS.md) | The Linux and macOS builds |
| [docs/LAUNCHER.md](docs/LAUNCHER.md) | The launcher and releases |

From the Vice City Stories port, in `docs/vcs-reference/` - VCS's addresses, but the same engine: the map of what to
look for in LCS and how it was done there:

| Doc | What's in it |
|---|---|
| [docs/vcs-reference/game-internals.md](docs/vcs-reference/game-internals.md) | Everything known about how the game works inside: code map, boot, camera, renderer, VU1 pipeline, 2D, streaming, sound, memory card - with addresses and how each fact was found |
| [docs/vcs-reference/NATIVE_PORT_ROADMAP.md](docs/vcs-reference/NATIVE_PORT_ROADMAP.md) | The plan to completion (milestones 1-8) and what "done" means |
| [docs/vcs-reference/PROGRESS.md](docs/vcs-reference/PROGRESS.md) | Status per area, open bugs, and the table of debug switches (`PS2X_*`, `VCS_*`) |
| [docs/vcs-reference/DECISIONS.md](docs/vcs-reference/DECISIONS.md) | Why things are the way they are; the user's decisions |
| [docs/vcs-reference/NATIVE_RENDERER.md](docs/vcs-reference/NATIVE_RENDERER.md) | The D3D11 renderer: layers, stages, HLE method |
| [docs/vcs-reference/SETTINGS_MENU_WIDESCREEN.md](docs/vcs-reference/SETTINGS_MENU_WIDESCREEN.md) | Settings registry / menu, Hor+ widescreen |
| [docs/vcs-reference/FULLY_NATIVE.md](docs/vcs-reference/FULLY_NATIVE.md) | The steps left until a normal frame is fully native, with their status - the working list |
| [docs/vcs-reference/PERFORMANCE.md](docs/vcs-reference/PERFORMANCE.md) | Frame rate well past 100 fps everywhere: how it is measured, what was done (numbers), what is next |
| [docs/vcs-reference/RSL_SUBMISSION.md](docs/vcs-reference/RSL_SUBMISSION.md) | Plan: render submission at the RSL level (milestone 7) - draws from the game's render calls instead of DMA packets |
| [docs/vcs-reference/DISC.md](docs/vcs-reference/DISC.md), [docs/vcs-reference/CONTROLS.md](docs/vcs-reference/CONTROLS.md) | Disc layout; keyboard / gamepad mapping |
| [docs/vcs-reference/TRAINER.md](docs/vcs-reference/TRAINER.md) | The trainer (F5): what it does, how (the game's script commands), its check |

**When you learn something about Liberty City Stories, write it into docs/LCS.md in the same change** (with its VCS
twin's address when there is one). Record addresses and how you found them. When something is weird, use judgment, fix
or work around it, and write it down (LCS.md for the game, PS2RECOMP_PATCHES for the runtime, docs/DECISIONS.md for
choices). Never edit docs/vcs-reference/ except to fix links: it is the VCS port as it was.

## What the project is

- GTA: Liberty City Stories (PS2, SLUS-21423 USA v1.01) statically recompiled to C++ (PS2Recomp, vendored and
  heavily modified in `third_party/PS2Recomp`). An independent project, started 2026-11-14 as a copy of the GTA: Vice
  City Stories native port (same engine): everything that is not game-specific (runtime, renderer, platform, settings,
  launcher, tools) carried over; the VCS game code is replaced part by part with LCS's (docs/DECISIONS.md).
- The game is `build/lcs.exe` (Linux / macOS `build/lcs`), its settings `lcs.ini`, its data `%LOCALAPPDATA%/LCSRecomp`
  (never the VCS port's `VCSRecomp`), the launcher `LCSLauncher`.
- Still named after VCS, to be renamed later (DECISIONS.md): the `vcs::` namespace, the `VCS_*` / `PS2X_*` switches,
  `tools/vcs_disc.py`, `src/disc/vcs_disc_id.h`, the launcher's `@vcs` events, the `.vcs-build-dir` file.
- Windows, Linux (docs/LINUX.md: clang, Vulkan; a plain host build) and macOS on Apple Silicon (docs/MACOS.md: Apple
  clang, Metal). The Steam Deck is postponed.
- Distribution is source-only, bring your own disc. **Never commit game code or data**: no ISO, no
  ELF, no generated C++ (`generated/`), no disassembly listings, no extracted assets. Analysis dumps go
  to `build/analysis/` (ignored). Function maps (names + addresses) and docs describing behaviour are
  fine.
- Authentic by default; every enhancement is an option (settings registry, `src/settings`).

## Layout

| Path | What |
|---|---|
| `src/main.cpp` | Launcher: ISO, settings registration, runtime start |
| `src/game/lcs_*` | Liberty City Stories' own code: `lcs_overrides.cpp` (`applyLcs`, the LCS override block: every LCS hook, with its `LCS_*` switch) |
| `src/game/vcs_*` | The VCS port's game code, **off for LCS** (registered for VCS's executable only) and kept as the model until each part has an `lcs_*` replacement, then deleted. Shared pieces still in use: `vcs_stream.cpp` (`StreamLayout`). What they are: overrides / hooks (`vcs_overrides.cpp`), native draw HLE (`vcs_hle_world.cpp`), display (`vcs_display.cpp`), the trainer (`vcs_trainer.cpp`), frame timing (`vcs_framerate.cpp`, per-frame mission meters `vcs_script_timing.cpp`, particles `vcs_particle_timing.cpp`, lightning / clouds / shake / other effects `vcs_effects_timing.cpp`), the player's controls (`vcs_input.cpp`; free aim `vcs_aim.cpp`; the pause menu's keys and mouse `vcs_frontend.cpp`), the game's music / effects levels as settings (`vcs_volume.cpp`), button prompts by device (`vcs_prompts.cpp`, its glyphs `vcs_glyph_art.cpp`), traffic / pedestrian density and draw distance (`vcs_world_detail.cpp`), the NPC drive-by's fire rate (`vcs_driveby.cpp`), mission scripts stuck above 30 fps (`vcs_script_fixes.cpp`) |
| `src/render/`, `src/rhi/`, `src/platform/` | Presenter + shaders (HLSL; DXBC by fxc, SPIR-V by `tools/hlsl_spirv.py`, MSL by `tools/hlsl_msl.py`), the RHI (backends `d3d11/`, `vulkan/`, `metal/`, `null/`; only they see a graphics API), SDL3 platform layer |
| `src/sound/` | Sony's libsd on the host (the native sound driver in `src/game/vcs_sound.cpp` drives the SPU2 through it) |
| `src/settings/`, `src/ui/` | Settings registry + lcs.ini, the in-game settings menu (F2), the trainer menu (F5) |
| `src/debug/` | Crash handler, watchdog, sampling profiler, self-test, memory watch, the F3 game-state panel (`game_info.cpp`, also `gamectl.py gameinfo`) |
| `third_party/PS2Recomp/ps2xRuntime` | The runtime (EE, VIF, VU1, GS HW backend, IOP host, stubs); our changes marked `[gta-recomp]` |
| `game/lcs/` | Recompiler config (`overrides.toml` -> generated `game.toml`), function map (`functions.csv`; `manual_functions.csv` for ones found by hand), VU1 upload list |
| `game/vcs/` | The VCS port's config, for reference (not used) |
| `tools/` | Python 3 tools (standard library only) - see below |
| `launcher/` | `LCSLauncher.exe` / Linux `LCSLauncher` / macOS `LCSLauncher.app` (a CMake project of its own, no third-party code: its own window - Win32, X11 / Wayland or Cocoa -, renderer - D3D11, Vulkan or Metal, one `ui.hlsl` -, DirectWrite / FreeType / Core Text text, its own icons, animated UI, after uppies): disc, build tools, build, play - a front end over `tools/` (docs/LAUNCHER.md) |

## Build and run

```
python tools/build.py                       # rebuild after code changes (--reconfigure after adding files)
python tools/build.py --recompile [ISO]     # disc -> generated C++ -> build (slow)
python tools/build.py --iop-tests           # SPU2 unit tests (reverb, interpolation, noise, PMON)
python tools/build.py --quick               # no LTO (= --opt none): fast relinks; kept until --opt lto
python tools/build.py --check-deps          # Linux: missing packages and the command that installs them; macOS: the
                                            # Command Line Tools (docs/MACOS.md)
python3 tools/shader_tools.py               # macOS: glslang + SPIRV-Cross for the Metal shaders (the build runs it)
python tools/build.py --opt none|lto|pgo    # game optimisation, kept until changed (lto: the default; pgo: ThinLTO + profile, trains if needed)
python tools/bench.py [OUT]                  # game-thread benchmark: fixed city spots + an AI drive (game / worker ms)
python tools/cruise.py run [OUT] [ENV=VAL]   # stutter rig: the car along a fixed route at 144 km/h, every frame timed;
                                             # hitches with their texture / main-thread / game-thread cause (PERFORMANCE.md 21)
python tools/clean.py [--apply] [--keep-days 7]   # delete test output in build/ (lists first; builds, saves, baselines kept)
python tools/build.py --launcher             # the launcher (LCSLauncher(.exe) in the root; no disc, ~20 s the first time)
python3 tools/build.py --install-deps        # Linux: the missing packages through pkexec (what the launcher's button runs)
python tools/build.py --recompile-if-needed  # recompile only when generated/ is stale (generated/recompile_stamp.json)
python tools/doctor.py [--json]              # folder, compiler, disc, build state, the next step (the launcher reads --json)
python tools/build_dir.py [check|set P|reset] # the build folder elsewhere (.vcs-build-dir, VCS_BUILD_DIR): tc.BUILD follows;
                                             # the test tools keep writing to build\ in the source folder
python tools/release.py [--ref vX | --worktree]   # the Windows release zip (build/dist); refuses game files. The number is
                                             # VERSION (root; 0.3.0): bump it before tagging - the tag must match;
                                             # the notes: docs/vcs-reference/releases/<tag>.md (else GitHub generates them)
python3 tools/release.py --platform linux [--worktree --allow-new-glibc]   # the Linux AppImage (CI builds it on
                                             # Ubuntu 22.04 for glibc 2.35; a v* tag publishes zip + AppImage together)
python3 tools/release.py --platform macos [--worktree]   # the macOS .dmg: the app next to an Applications link
                                             # (tools/dmg.py writes the Finder layout; `dmg.py show X.dmg` prints it)
python tools/deps.py list|fetch|check        # the libraries CMake downloads; deps/ makes the build offline
python tools/backup.py create|list|restore   # memory card backups (VCS_DATA_DIR honoured: test on copies)
python tools/setup_vs.py status|install [--dry-run]   # Visual Studio Build Tools (+ clang) from Microsoft, elevated
python tools/hlsl_dxbc.py ... [--check-fxc FXC]       # HLSL -> DXBC through Windows' d3dcompiler_47.dll (no Windows SDK)
python tools/update.py check|apply [--zip F]  # a release folder to the newest GitHub release (not a clone)
python tools/rip_disc.py list|rip E: OUT.iso  # a PS2 disc in a DVD drive to an .iso; report.py zips logs for bug reports
python tools/framerate_audit.py [--steps 15,30,60,100]   # behaviour per simulated second at forced time steps
python tools/run.py 60 name                 # run 60 s with the window, log to build/run/name.log (0 s: until it exits; --hidden for tests)
python tools/run.py 60 name --control       # + control channel for tools/gamectl.py
python tools/scenes.py capture OUT          # reference scenes from an empty card (screenshots + perf)
python tools/scenes.py compare REF NEW      # picture / perf diff of two captures
python tools/gamectl.py press F2|screenshot f.png|perf|quit
python tools/gamectl.py gameinfo           # player position / heading / state, vehicle, camera, clock, pauses, scripts (F3's panel)
python tools/rig.py start --save build/rig/saves/fortbaxter   # straight into gameplay from a card copy
python tools/rig.py player|scripts|peekf 0x4e4910 1|scm <hex>  # inspect / drive the running game
python tools/tour.py OUT [--every 9 --limit 18]               # world tour (quick subset with --every/--limit)
python tools/saves.py [OUT]                                   # save / Load Game / Delete Game through the menus
python tools/soak.py [OUT] --hours 3                          # hours of autodrive: crash / hang / leak / falling
python tools/memcheck.py [OUT] [--minutes 15]                 # memory over a route that streams a lot must level off
                                                              # (the RSL recordings' limit, GitHub issue 115)
python tools/shakedown.py [OUT]                               # mission 50: cutscene + shop entry (the "Loading..." hang)
python tools/haze_check.py [OUT] [--timing 1 --fps 144]       # mission 94 at 60 fps: the cutscene that stuck on "Loading...",
                                                              # the haze across a 16:9 picture
python tools/soundcheck.py all OUT [--mission N]             # native sound vs the emulated IOP: record, replay, compare
python tools/hose_check.py [OUT] [--step 144]                 # Hose the Hoes at a simulated frame rate: the girls' bar,
                                                              # the parked cars' fuses (the hose: framerate_audit --only hose)
python tools/trainer_check.py [OUT] [--vehicles all]          # every trainer feature, checked in the game's memory
python tools/finale_check.py [OUT] [--story] [--full]         # Last Stand's end from the trainer: final cutscene, credits
python tools/controls_check.py [OUT]                          # bindings, presets, reset, vibration, volume levels; menu screenshots
python tools/hud_check.py [OUT]                               # HUD at the screen edges: radar left, centred text in the middle
python tools/menu_check.py [OUT]                              # the pause menu: Escape / Enter, the mouse (hover, click, sliders,
                                                              # map drag), the gamepad as before
python tools/prompts_check.py [OUT]                           # button prompts: each glyph set in help text and the menu, the
                                                              # device switch, Original unchanged
python tools/irxdis.py --decompile X.IRX OUT.c                # an IOP module as C (headless Ghidra; analysis only)
python tools/eedecomp.py 0x2f7d58 [...] [--out FILE]          # game functions as C (the analysed EE project; analysis only)
```

**For LCS today**: the build, run, gamectl (press / screenshot / perf / peek), eedecomp, xmatch, doctor, clean and backup
tools work. The game checks above (bench, cruise, tour, saves, soak, memcheck, shakedown, haze / hose / trainer / finale
/ controls / hud / menu / prompts checks, soundcheck, framerate_audit, scenes, the rig's script commands) drive VCS's
missions, memory and script VM at VCS's addresses: each needs its LCS addresses before it means anything here.

The rig (`src/debug/rig.cpp`, `tools/rig.py`) drives the game through its own script VM: `Game.teleport`,
`set_time`, `weather`, `invincible`, `mission(n)` and raw `scm(bytes)` (assembler: `rig.Scm`, VCS opcode numbers
from the Sanny Builder Library, checked against the handler table - game-internals.md 11). `build/rig/saves/` holds
card copies to start from (never the user's live card): the test tools take `fortbaxter/`, else `user/` (a copy of the
player's card; `rig.default_save`).

- Test runs are **hidden** (the user's choice, 2026-10-24): no window, physical input ignored, sound on (the user
  mutes the game in Windows); all launches go through `toolchain.test_window`; the rig passes `run.py --hidden`.
  `VCS_TEST_VISIBLE=1` shows the window when asked. `run.py` on its own shows the window: the user starts the game
  with it too.
- `VCS_DATA_DIR=<dir>` gives a run its own memory card / cache. **Never run tests against the user's
  card** (`%LOCALAPPDATA%/LCSRecomp/mc0`) when they could write to it; copy it with
  `robocopy /E /COPY:DAT /DCOPY:T` (keeps creation times, which the card order depends on).
- `lcs.ini` next to `lcs.exe` holds the user's settings; environment overrides (`VCS_ASPECT=169`)
  apply to one run without touching it.
- Rebuild costs: a change to `ps2_memory.h`, `gs_backend.h`, `ps2_hle_survey.h` or anything the
  generated code includes rebuilds everything (~6-10 min); runtime-only changes ~1 min; `src/` only
  seconds. Prefer `src/game/lcs_overrides.cpp` for experiments.
- Test output (screenshots, logs, card copies) goes under `build/<tool>/<name>`; it adds up (23 GB by 2026-10-23,
  mostly screenshots). Run `python tools/clean.py --apply --keep-days 7` after a session of test runs. A capture
  worth keeping as a reference goes to `build/scenes/reference` (scenes compare) or `build/regress/baseline`.
- Optimisation (`--opt`): a fresh build directory is `lto` (ThinLTO: what players get from the README). `--quick`
  (= `none`) for day-to-day work (fast relinks); the choice stays in the CMake cache, and the last line of a build says
  which one it is. `pgo` / `lto` relink slower and are for measuring and releases. A PGO profile from older code still
  builds; retrain (`--opt pgo --train`, ~30 min) after large changes. Never edit sources while a build or the PGO
  pipeline is compiling them.
- Windows compiler (DECISIONS.md 2026-11-03): llvm-mingw, the default (`toolchain.WINDOWS_TOOLCHAIN_DEFAULT`): clang +
  lld + libc++, the GNU driver like Linux; pinned in `toolchain.LLVM_MINGW_DOWNLOADS`, downloaded into
  `build/build-tools`; no Visual Studio, no Windows SDK. `VCS_TOOLCHAIN=msvc` builds with Visual Studio's clang-cl, kept
  for comparison. Shaders come from `tools/hlsl_dxbc.py` either way (no fxc). Switching compilers sets the tree up from
  scratch (`toolchain.drop_other_compiler`); compare the two in their own folders (`VCS_BUILD_DIR=build/msvc`). Code
  that differs: `__MINGW32__` / CMake's `MINGW`; `_MSC_VER` means clang-cl only. libc++ on Windows has a slow
  `steady_clock::now`, timed waits that spin their last millisecond and a CRITICAL_SECTION `recursive_mutex`: hot code
  uses `ps2x::hostNow` / `waitUntil` / `waitFor` / `RecursiveMutex` (`runtime/ps2_host_std.h`, PERFORMANCE.md 29).
- A successful build writes `iso=<disc>` into the `lcs.ini` next to `lcs.exe` when that file has none, so the exe
  started on its own finds the disc.
- The launcher (`launcher/`, docs/LAUNCHER.md) only runs `tools/`: build logic stays in Python, so a change to the
  pipeline is a change to the scripts. The tools print `@vcs {json}` event lines (`toolchain.event` / `stage` / `fail`
  with a code) the launcher reads; keep them when changing a tool's output. `LCSLauncher.exe --screenshot F.png
  [--screen home|task|sheet:saves|gallery... --demo error --frames N --action build]` renders its window offscreen
  (tests, docs; `--screen gallery` shows every widget); `--hidden --quit-after S` runs the real loop unshown and prints
  its cost; `--self-test` is CI's check. The launcher's UI is immediate mode: never open a file dialog inside a frame
  (`screens::runPending`), and keep it idle - no animation that runs forever while nothing happens, except Home's
  backdrop (the user's wish, 2026-11-01: 30 fps, off while the game runs and with reduce motion; DECISIONS.md).

## Release notes

Each release's notes are `docs/releases/v<VERSION>.md`. CI publishes them as the release's text and sets the title
itself ("GTA Liberty City Stories Launcher vX"), so the file has no heading. Follow the VCS port's format
(`docs/vcs-reference/releases/v0.2.1.md`; that user's choice, 2026-11-04). LCS has no release yet (VERSION 0.1.0):

- **Sources:** `git log <previous tag>..HEAD`, with the commit bodies. Group the changes by what a player notices, not
  by commit. Leave out anything a player never sees (tools, CI, docs, test rigs) unless it changes how they build,
  update or report a problem.
- **Layout:** the first line is `**Full Changelog**: https://github.com/<owner>/<repo>/compare/vPREV...vNEW`
  (the LCS repository on GitHub, once there is one).
  Sections are plain lines, not Markdown headings, each followed by bullets. The biggest change comes first as
  `New: <what it is>`. Then come sections named by topic (`The car camera`), then `Fixes`, then `Launcher` (or `Also`).
  The last line is an `Updating:` paragraph: the Windows steps in the launcher, that saves aren't touched, extracting
  into a new folder as the other way, and the Linux AppImage.
- **Writing:** write for players, in plain words. Say what they see or do now, and what it was like before ("Before,
  it moved slowly and fought you."). Name settings in bold with where to find them ("the game's settings (F2) →
  Keyboard: **Car camera sensitivity**") and give their defaults. Put a GitHub issue at the end of its bullet as
  `(#N)`. Prefix a bullet that only applies to one platform or one mission with its name (`Linux:`, `Farewell To
  Arms:`). Never use function names, addresses, environment variables or internal file names.
- **Be honest:** if a fix couldn't be reproduced here, say it's hoped to fix the problem. Tell updaters about costs:
  a full rebuild, a large download (with its size), or a feature they'll only see from the next update on.
- **Before tagging:** bump `VERSION` to the tag's number, and commit the notes in the same commit. Never put `[skip ci]`
  in the message of the commit the `v*` tag points at: GitHub then skips the tag's workflow, so no release is made.
  If that happens anyway, `gh workflow run launcher.yml --ref vX` builds and publishes it.

## Rules that keep things working

1. **Reference first.** A native replacement of an emulated path is checked against it (bit-exact
   where possible: `PS2X_HLE_VERIFY=1`; picture diff via scenes otherwise) and keeps a switch back
   (`PS2X_HLE=0`-style). The emulated path is never deleted.
2. **Hooks on recompiled functions** (DECISIONS.md): a generated function may return to the EE
   scheduler mid-body and be resumed through the function table, bypassing a wrapper. A hook may work
   *before* calling the original or replace it entirely; never rely on code after the original
   returns. Never call a guest function directly from host code expecting it to run to the end -
   chain it as a guest tail call (see `VCS_TEST_SAVE`).
3. **Floating point**: no FP contraction in generated / VU code; VU maths rounds toward zero
   (`FE_TOWARDZERO` where reproducing it). Bit-exactness depends on both.
4. **Never run `PS2X_GS=gpu`** (paraLLEl-GS) on the dev machine: it hangs the GTX 1080 Ti into a TDR
   reset and blanks the user's screen. `hw` (D3D11) is lcs.exe's own default.
5. **Patch scripts**: shell heredocs on this machine turn `\n` inside Python strings into real
   newlines and corrupt C++ string literals. Use the Edit tool, or write the script to a file first.
6. **Scripts are Python 3, standard library only**, runnable from any shell; extend `tools/*.py`
   (shared helpers in `tools/toolchain.py`) instead of adding shell scripts.
7. **Runtime changes** in `third_party/PS2Recomp` get a `[gta-recomp]` marker and a row in
   PS2RECOMP_PATCHES.md.
8. **Commit** at every milestone / major change, with a message that says why. End commit messages
   with the attribution line the environment asks for.

## Recognising old problems

- Wrong colours on every texture: data shifted by VIFcodes (VIF1 DIRECT continuation, fixed).
- Stuck on a loading screen, no crash: an IOP-side wait (`VCS_TRACE_DELAY`, `PS2X_TRACE_SPU`).
- Thread "returns" to a float bit pattern (`0x3F333333`...): a saved `$ra` overwritten - the
  checkpoint-unwind class of bug (`VCS_WATCH=lo,hi,...`).
- Saves written but never found: card directory order (game-internals.md 10c).
- A mission stuck after a ped should talk, or peds vanishing: check the mission audio slots and who deletes the ped
  (`VCS_TRACE_PED_REMOVE`, game-internals.md 12); Shakedown's was the recompiler's 32-bit compare-with-zero branches.
- "Loading..." for ever in the middle of a scripted cutscene, at 60 fps and not at 30: the mission script waiting for
  an audio line whose speaker died behind the fade (its script sits at 0xA9F9 - 0xAA0B; Purple Haze's thug run over by
  a truck that only keeps rolling at shorter frames - game-internals.md 11h, `src/game/vcs_script_fixes.cpp`). The
  wait ends by itself now (`[script-fix] mission audio slot ... was freed` in the log): look for what killed the speaker.
- Something wrong only above 100 fps: a `ms_fTimeStep < 0.5` branch the PS2 never takes (game-internals.md 4).
- An effect faster, shorter or busier at high frame rates (smoke, sprays, flashes, flicker, trails): it counts frames,
  not time - game-internals.md 4 "Effects that run per frame" lists the known ones; `VCS_PARTICLE_LOG=1` shows which
  particle call sites run per frame (their calls per second grow with the frame rate).
- A mission bar / meter / timer faster at high frame rates: the mission script adds a fixed amount every frame; add
  the instruction to `src/game/vcs_script_timing.cpp` (game-internals.md 11f has the survey and how to read a mission).
- Load Game stuck on "Loading..." then a return to text (`0x4548435F`) from the script VM: host code took a game
  script for one of its own (a remembered script slot the load had refilled; game-internals.md 11d, `[rig] script
  slot` in the log).
- "Loading..." over black after a mission's last cutscene, the game running behind it (clock and scripts move): the
  screen left faded out for a script that never comes - Last Stand out of story order waits for the credits
  (game-internals.md 11g). F12's draw log (`PS2X_HW_DUMP_KEY=1`) shows the black quad and the text; the active scripts
  show who is missing.
- `ninja: error: manifest 'build.ninja' still dirty after 100 tries`: files dated after the clock (a zip from another
  time zone); `build.py` sets them to now (`toolchain.fix_future_files`, docs/LAUNCHER.md).
- A tool fails within a second, its log empty (a player's report, 2026-10-31): a crash before the first line - the log
  now ends with the exit code (`0xC000001D` illegal instruction: a build for a newer CPU, `PS2X_CPU`; DECISIONS.md).
- `[main] fatal exception: No mapping for the Unicode character exists in the target multi-byte code page`, or a path
  full of `?`s: a non-ASCII folder seen through the ANSI code page - lcs.exe's UTF-8 manifest missing or ignored
  (`src/lcs.manifest`, DECISIONS.md 2026-11-01).
- A save gone after saving (an empty slot file, or the whole save folder deleted): a card name the host could not
  store, or a failed card write - the game deletes its save folder after one (game-internals.md 10f). The card is
  zipped into `<data>/backups/auto-*.zip` before every change (`[MC] card backup` in the log).
- `Could NOT find Python3` in configure.log: CMake left to find a Python of its own (a release's is in `python/`,
  not on PATH); the build passes its own (`toolchain.python_defines`, DECISIONS.md 2026-11-05). A build that stops
  with no log in a long folder (Quiver's): MAX_PATH while unpacking a tool (`toolchain.long_path`).
- Memory growing by gigabytes over hours of play (GitHub issue 115): a cache keyed by guest address with no bound - the
  game streams the same model in at another address, so such keys never stop coming. The RSL recordings were one
  (bounded since 2026-11-10, `VCS_RSL_RECORDINGS_MB`); `VCS_RSL_WORLD_LOG=1` prints what they hold, `tools/memcheck.py`
  is the check (PERFORMANCE.md 31).
- First things to grep in a log: `[ee] thread N -> dormant`, `[guest-branch:unlisted-resume]`.

## Working style for long autonomous runs

- Follow docs/vcs-reference/NATIVE_PORT_ROADMAP.md milestone by milestone without pausing for routine decisions;
  record decisions in DECISIONS.md and progress / numbers in PROGRESS.md.
- Measure before and after anything on the hot path (perf lines from `gamectl.py perf`, scenes'
  `perf_*.txt`); keep the numbers in the docs.
- Keep the regression rig (milestone 1) green; add a check when you fix a bug so it stays fixed.
