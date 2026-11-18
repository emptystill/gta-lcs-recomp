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

## 2026-11-16: the LCS trainer through the game's script VM

The weapons went in as the cheats' own guest calls, chained from the frame boundary. For the rest (proofs, vehicles of
every kind, warps, the clock, the weather) the game's script commands do what is needed the way its missions do, so the
trainer runs short scripts on LCS's VM as VCS's trainer does on VCS's. LCS's VM had to be found for it (its code
differs from VCS's; GTA3's layout and numbering, docs/LCS.md "The script VM"); each command the trainer uses was read
in its handler rather than taken from a list. Weapons stay on the chained calls: they work and give a set in one frame.
Missions and the free camera are not on LCS's trainer yet.

## 2026-11-17: the menus are Liberty City Stories' own

The user does not want anything of the VCS port in what the player sees. The settings menu (F2) and the trainer (F5):

- **Look**: Liberty City Stories' pause menu - its red titles, the tab bar's orange, the options' pale blue on a
  night-blue dark (`src/ui/theme.cpp`); titles in a heavy face (Impact), text in a condensed one (Bahnschrift), both
  from the system (Windows; DejaVu / Arial on Linux / macOS), no font shipped. The trainer's Vice City sunset header
  became the game's red with the orange rule.
- **Language**: English or Spanish, chosen in Game > Language / Idioma (Spanish by default: the user's game is Spanish).
  Every text goes through `ui::i18n::tr` (`src/ui/i18n.cpp`, the table `src/ui/i18n_es.inc`, English as the key);
  `LCS_I18N_DUMP=<file>` lists the settings' texts the table lacks. The trainer's notices are translated where they
  are shown; the control channel's answers stay English for the tools.
- **Settings that do nothing on LCS are gone**: music / effects volume (VCS's levels; back with LCS's sound), movie
  volume and Skip intro movies (LCS's movies are always skipped), Cutscene black bars, World (traffic, peds, draw
  distance), the NPC drive-by fire rate, Reduce Post Processing Effects (VCS's effect timing), the free camera's key and
  the gamepad's free aim. Each comes back when its LCS version exists.
- Environment overrides show as `LCS_*` (`settings::add`); the `VCS_*` names still work for the test tools.
- **The launcher too** (the user: "es hora cambiar el aspecto del launcher"): the same orange / red / night-blue
  palette, Home's Vice City dusk replaced by a comic-book cover in the style of LCS's box art (the user's pick of three:
  cover, skyline at night, pause-menu look), drawn in code like the old scene (no image shipped), a fedora for the
  logo and icons. Its language follows the game's menus (`ui.language` in lcs.ini) with a selector of its own in
  Settings; one setting for both, so the player chooses once (docs/LAUNCHER.md).
- **The game's settings in the launcher** (the user: "que el launcher tenga las opciones del menu que tiene f2"): a
  Game sheet with every F2 page. The game describes its own registry (`lcs.exe --settings-json`) instead of the
  launcher keeping a copy of the list, so a setting added to the game shows in the launcher with no launcher change;
  the launcher edits lcs.ini only while the game is closed.
- **Home's pictures and cards** (the user, 2026-11-17: three pictures and the logo for the cover, "Recompilado para
  PC", cards unlike VCS's launcher): the pictures load from `launcher/art`. First kept out of git (Rockstar's and a fan
  artist's work); the user wants them in the repository and the releases ("no tengo pensado monetizar"), so they are
  committed - cover-1 shrunk to 1600 px high (the launcher stores it at 1200 at most). Without them Home is the drawn
  cover as before. The three cards became slanted
  comic panels - black border, a hard offset shadow and a tag in the cover's red / orange / blue, titles in the heavy
  face - where VCS's are rounded glass cards.
- **F2 off, the game's own menu instead** (the user, after the Game sheet: "podemos quitar el menu que viene del
  juego... a menos que algunas opciones se puedan integrar nativamente"): the pause menu's table has room, so Aspect
  ratio, Widescreen HUD, Frame rate cap (Display) and Master volume (Audio) are rows of the game's menu
  (docs/LCS.md "Our settings in the pause menu"); everything else, the restart-only settings among them, is the
  launcher's. The F2 menu stays in the code, off by default (`ui.f2_menu`), for whoever turns it on.

