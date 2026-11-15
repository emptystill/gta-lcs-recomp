# GTA: Liberty City Stories — Native PC port (PS2 static recompilation)

A native PC port of **Grand Theft Auto: Liberty City Stories** (PS2, USA, SLUS-21423 v1.01), made by recompiling the
game's PS2 executable into C++. Windows first; Linux (Vulkan) and macOS (Metal) build from the same source.

**This repository contains no game code or game data.** You need your own copy of the game as an `.iso`.
The build reads the executable from your disc, and the finished game reads everything else from it while it runs.

**Status: early.** The game boots, plays its intro cutscene and goes into gameplay, with the PS2's look and 30 fps.
The enhancements (widescreen, high frame rates, mouse and key bindings, native rendering and sound, the trainer) are
being brought over one by one. What works and what is next: [docs/LCS.md](docs/LCS.md).

This port is built on the **GTA: Vice City Stories native port** (the two games share Rockstar Leeds' engine): its
runtime, renderer, platform layer, launcher and tools came from there, and its documentation is kept in
[docs/vcs-reference/](docs/vcs-reference) as the map of what to look for in Liberty City Stories.

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

- Controls for now: the runtime's own pad mapping - arrows / WASD to move, Shift = Cross, Enter = Start,
  Space = Square, F = Triangle; a gamepad works as a PS2 pad.
- **F2** opens the settings (resolution, window, sound devices); many of its options do nothing in LCS yet.
- **Alt+Enter** switches between full screen and the window; **F8** saves a screenshot.
- Saves (the memory card) live in `%LOCALAPPDATA%\LCSRecomp` on Windows, `~/.local/share/LCSRecomp` on Linux -
  never in the Vice City Stories port's folder.

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

## License

GPL-3.0 (inherited from PS2Recomp). Grand Theft Auto and all game content are property of Rockstar Games /
Take-Two Interactive.
