# Liberty City Stories on the VCS port

This tree is a copy of the GTA: Vice City Stories native port (2026-11-14), turned towards GTA: Liberty City Stories
(PS2, USA, SLUS-21423 v1.01). The two games share Rockstar Leeds' engine (LCS is the older one) and the same Sony SDK
build, so the recompiler, the runtime, the platform layer and most tools carry over unchanged; the game code in
`src/game/` knows VCS's addresses and is off for LCS until each part has its LCS addresses.

Everything else in `docs/` describes VCS and is kept as the map of what to look for: the same kinds of functions,
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
- The launcher (`launcher/`) still names VCS everywhere: not used for LCS yet (the tools are run directly).

## Status

See "Log" below for each step.

## Log

- 2026-11-14: tree copied, tools pointed at SLUS_214.23 / `game/lcs`, Ghidra + analyzer run, overrides found.
