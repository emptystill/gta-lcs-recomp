# GTA: Liberty City Stories — Native PC port (PS2 static recompilation)

A native PC port of **Grand Theft Auto: Liberty City Stories** (PS2, USA, SLUS-21423 v1.01), made by recompiling the
game's PS2 executable into C++. Windows first; Linux (Vulkan) and macOS (Metal) build from the same source.

**This repository contains no game code or game data.** You need your own copy of the game as an `.iso`.
The build reads the executable from your disc, and the finished game reads everything else from it while it runs.

**Status: first release (0.1.0), playable.** The story plays from the start, in widescreen (Hor+) at high frame rates
(up to your display's refresh, or uncapped), with keyboard and mouse (a free mouse camera, mouse aim), the gamepad,
the trainer (F5) and the launcher. Still to come: the frame-rate fixes for mission timers and some effects (things
that count frames may run faster above 30 fps), native sound, world detail options. What works and what is next:
[docs/LCS.md](docs/LCS.md).

## Play from a release

Download the zip from [Releases](../../releases) (Windows; the AppImage on Linux, the .dmg on macOS), extract it into a
folder of its own and start `LCSLauncher`. It asks for your disc image, gets the build tools, builds the game from your
disc (the first build takes a while) and starts it. It also updates itself and the build, and backs up your saves.

Built on the work of others: see [Credits](#credits).

## Build

### 1. Tools (once)

**Windows**: Python 3 ([python.org](https://www.python.org/downloads/), tick *Add python.exe to PATH*) and Git
([git-scm.com](https://git-scm.com/download/win)). There's no compiler to install: the first build downloads
[llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (~190 MB, into `build\build-tools`), and CMake / Ninja when none
is found.

**Linux**: Python 3 and Git, then `python3 tools/build.py --check-deps` lists what is missing (clang 19+, lld,
glslang, FFmpeg and SDL3 development packages) and the command that installs it ([docs/LINUX.md](docs/LINUX.md)).

### 2. Your disc

Put your disc image in the repository folder or its `iso/` folder. It must be the USA release (SLUS-21423), and its
file name must contain `Liberty City Stories`. Check it with:
```bash
python tools/vcs_disc.py info "iso/Grand Theft Auto - Liberty City Stories (USA).iso"
```

### 3. Build and play

```bash
python tools/build.py --recompile        # disc -> C++ -> build/lcs.exe (the first time takes a while)
python tools/run.py 0 play               # play, with the output logged to build/run/play.log
```
Or start `build\lcs.exe` directly: the build already told it where your disc is (`iso=` in `build\lcs.ini`).
`LCSLauncher.exe` (`python tools/build.py --launcher`) is the same pipeline with buttons.

- Controls: WASD to move, the mouse turns the camera and aims, left click fires, F enters vehicles; every key can be
  rebound. A gamepad works as a PS2 pad. Button prompts follow the device you use.
- Settings: aspect ratio, widescreen HUD, frame rate cap and master volume are in the game's own pause menu
  (Display / Audio); everything else is in the launcher's **Game** page. The older F2 settings menu can be turned on
  there.
- **F5** opens the trainer (weapons, vehicles, teleports, time, weather, god mode).
- **Alt+Enter** switches between full screen and the window; **F8** saves a screenshot.
- Saves (the memory card) live in `%LOCALAPPDATA%\LCSRecomp` on Windows, `~/.local/share/LCSRecomp` on Linux.

## Problems

| What you see | What to do |
|---|---|
| `no disc image` | Put the `.iso` in the repository folder or `iso/`, or give its path after `--recompile`. |
| `unsupported executable build` / `SLUS_214.23 not found on disc` | The disc isn't the USA release (SLUS-21423). Other regions aren't supported. |
| `lcs.exe` does nothing when started | It can't find the disc: put `iso=C:\path\to\your.iso` in `lcs.ini` next to `lcs.exe`. |
| The game stops with `[guest-branch:missing-target]` in the log | A function the analysis missed: one line in `game/lcs/manual_functions.csv` ([docs/LCS.md](docs/LCS.md)). |

`python tools/doctor.py` checks everything at once; `python tools/report.py` zips the logs for a bug report (no game
files, no saves).

## For developers

[AGENTS.md](AGENTS.md) is the guide to working on the code; [docs/LCS.md](docs/LCS.md) is where Liberty City Stories
stands and how it was brought up.

| Path | What |
|---|---|
| `third_party/PS2Recomp` | Vendored PS2 recompiler + runtime (GPL-3.0), changes marked `[gta-recomp]` |
| `game/lcs` | Recompiler config and function map for SLUS_214.23 |
| `src/game/lcs_*` | Liberty City Stories' own hooks |
| `src/game/vcs_*`, `game/vcs` | The Vice City Stories port's game code: off for LCS, replaced part by part |
| `src` (the rest) | Renderer, platform layer, settings, sound, debugging |
| `tools` | Disc reader, build pipeline, launcher back end, test and analysis scripts |
| `launcher` | The launcher (its own renderer and UI; runs the tools) |
| `docs` | LCS status, the platforms, the runtime's patches; `docs/vcs-reference` for VCS |

## Credits

- **[GTA: Vice City Stories native port](https://github.com/gothamor/gta-vcs-recomp)** by gothamor: this port started
  as a copy of it (the two games share Rockstar Leeds' engine). Its runtime changes, renderer, platform layer,
  settings, launcher and tools are the base of everything here, and its documentation is kept in
  [docs/vcs-reference/](docs/vcs-reference) as the map of what to look for in Liberty City Stories. The code that
  still carries `vcs_` in its name comes from it.
- **[PS2Recomp](https://github.com/ran-j/PS2Recomp)** by ran-j and its contributors: the PS2 static recompiler and
  runtime (vendored and modified in `third_party/PS2Recomp`; the changes are listed in
  [docs/PS2RECOMP_PATCHES.md](docs/PS2RECOMP_PATCHES.md)).

This project is not affiliated with or endorsed by Rockstar Games or Take-Two Interactive.

## License

GPL-3.0 (inherited from PS2Recomp and the Vice City Stories port). Grand Theft Auto and all game content are property of Rockstar Games /
Take-Two Interactive.
