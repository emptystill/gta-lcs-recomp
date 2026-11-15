# Decisions - the Liberty City Stories port

Why things are the way they are. The Vice City Stories port's decisions (the runtime, the toolchain, the launcher, the
user's choices there) are in [vcs-reference/DECISIONS.md](vcs-reference/DECISIONS.md) and still hold for what came
from it, unless a decision here replaces one.

## 2026-11-14: a copy of the VCS port, not a second game in it

The user asked whether the VCS port could port LCS. The two games share Rockstar Leeds' engine and the same Sony SDK
build, so the runtime, recompiler, renderer, platform layer, launcher and tools carry over; only the game code
(`src/game/`, `game/<game>/`) knows addresses. The port was copied into its own folder and git repository
(`C:\Games\GTA Liberty City Stories`) instead of teaching one tree both games: the VCS port stays exactly as it was
(the user plays it), and LCS can change anything without breaking it. The cost: runtime fixes made in one no longer
reach the other on their own; carry them across by hand (`docs/PS2RECOMP_PATCHES.md` lists every runtime change in
both).

## 2026-11-14: an independent port (the user's wish)

The user wants LCS to be a port of its own, not "VCS with LCS in it". In three phases:

1. **Identity (done 2026-11-14)**: the game is `lcs.exe` with `lcs.ini`, its data `%LOCALAPPDATA%\LCSRecomp`, the
   launcher `LCSLauncher` and its texts, the README and AGENTS.md about LCS, VERSION back to 0.1.0 (no LCS release
   yet), the VCS docs moved to `docs/vcs-reference/` (kept unchanged as the map of what to look for), the VCS release
   manifest (`RELEASE.json`: `tools/update.py` would have fetched VCS releases into this folder) and leftover files
   from older VCS releases removed.
2. **Game code, part by part**: each VCS feature (`src/game/vcs_*`: widescreen, frame rate, input, native rendering,
   native sound, trainer...) is redone as `src/game/lcs_*` with LCS's addresses (`tools/xmatch.py` first), and the VCS
   file is deleted when nothing uses it any more. Not all at once: the VCS code is the guide to what to look for.
3. **Internal names last**: the `vcs::` namespace, the `VCS_*` switches, `tools/vcs_disc.py`, `src/disc/vcs_disc_id.h`,
   the launcher's `@vcs` events and `.vcs-build-dir`. Renaming them is mechanical but touches hundreds of files, and
   is cheapest once phase 2 has removed most of the VCS code that uses them. Then `tools/compat_symbols.py` (stubs
   for the VCS functions `src/` names) goes too.

Not changed in phase 1: the launcher's look (Home's sunset and palm-tree city, the pink theme, its icon) is Vice
City's; an LCS look is a design job of its own.

## 2026-11-14: the runtime's allocations outside the game's heap

`SetupHeap` made the runtime's `guestMalloc` heap the game's own heap region, and the runtime's `sceGsResetGraph` stub
wrote its GIF packet over LCS's live data (docs/LCS.md). The runtime's heap is now a fixed area LCS never uses
(0x40000..0x78000, `src/game/lcs_overrides.cpp`; `PS2X_RUNTIME_HEAP` in the runtime). `LCS_RUNTIME_HEAP=0` undoes it.
