# Trailer footage

`tools/trailer.py` records the trailer's shots from the game as video files: scripted, repeatable, at any resolution,
with one game frame per video frame. The edit (music, titles, cuts) is done in a video editor; the script plan the user
works from is kept with the clips (`build/trailer/GUION.md`, not in the repository).

```
python tools/trailer.py list                       the shots
python tools/trailer.py shoot [SHOT ...]           record them to build/trailer/<shot>.mp4 (all by default)
          [--size 2560x1440] [--save DIR] [--env K=V ...] [--keep-going]
```

## How a shot is made

- **A hidden game run per shot**, from a copy of a memory card (`build/rig/saves/user` by default: a copy of the
  player's card, made with `robocopy /E /COPY:DAT /DCOPY:T`; never the live card). LCS loads the card's newest save by
  itself at boot - free roam at the save's safehouse, no mission (with an empty card a new game starts, and its first
  mission holds the wanted level down).
- **The scene** through the trainer's control commands: place (a landmark), hour, weather, weapons, vehicle, wanted
  level, god mode, unlimited ammo. Commands added for this (`lcs_trainer.cpp`): `heading DEG` / `carspeed V` (the
  player's vehicle), `goto X Y Z`, `nearcars` (the vehicle pool around the player, nearest first), `roadcar` (a traffic
  car driving 15-120 m away: a road and its direction).
- **Roads**: a driving shot puts the player's car 20 m ahead of a traffic car seen driving, facing the same way (a
  search of eight headings from the landmark found the pavement more often than the road). Only cars within 6 m of the
  player's height: one in the tunnel under Chinatown (20 m down) put the player in the rock and killed him. `street`
  does the same on foot (vistas); `start = (x, y, z, heading)` fixes the place - 01-03 share Saint Mark's road, so the
  4:3 -> widescreen cut lands on the same street.
- `camera heading H` looks along a heading (the trainer's degrees).
- **Camera**: `camera behind` (`vcs_input.cpp`) turns the mouse camera behind the vehicle or Toni at the next frame;
  `mouse vel DX DY` adds the same mouse counts every game frame (even pans; `mouse DX DY` lands whole on whichever frame
  reads it next). Driving shots set `LCS_CAR_RETURN=10` (the camera back behind the car 1 s after the last turn).
- **Clean picture**: `LCS_TRAINER_NOTICES=0` hides the trainer's notices; recording starts 5.5 s after getting into a
  vehicle (the game shows its name).
- **Silent**: `LCS_VOLUME=0` (the master volume's override, new): the game runs at the encoder's pace, so its sound
  would be wrong anyway.

## The recorder (runtime)

`record start PATH [FPS]` / `record stop` / `record state` on the control channel (`ps2_runtime.cpp`): every new picture
shown (`gsHwShownInputNs` changed) is read back at the window's size (`present::capture`, as the screenshots) and
written raw (RGB) into an `ffmpeg` process on PATH: NVENC H.264, constant quality 16 (`PS2X_RECORD_CODEC` replaces the
codec arguments). The game is paced to it: `ps2control::recordGameFrame()` at each frame kick holds the game thread
while it is more than `PS2X_RECORD_AHEAD` (4) frames ahead of the pictures recorded (at most 1 s), and the shots run
with `VCS_TIMESTEP` = the video's rate, so each video frame is exactly one game frame of 1/60 s (1/30 for the PS2
look) whatever the encoding speed.

Measured (2560x1440, GTX 1080 Ti): 6 s of driving recorded in 18-22 s, 11 s of explosions in 29 s; frames written =
game frames (the last 4-5 still in flight at the stop), no duplicates (`mpdecimate` keeps all 365 of a 6 s walk).

Two things found on the way:
- **An ahead of 2 deadlocked** to the 1 s timeout on most frames: a picture is shown ~3 frames after its kick (the
  render worker and the GS stage). 4 never timed out.
- **Counting frames at the frame boundary double counted**: `rig::onGameFrame` runs again after every guest call the
  trainer chains there (god mode / unlimited ammo write every frame), so the recorder waited for pictures that never
  came (119-374 timeouts a shot). The count is at the frame kick (`lcs_overrides.cpp frameKick`), once a frame.

## Looks

| | ps2 | pc |
|---|---|---|
| Aspect | 4:3 (`VCS_ASPECT=43`) | 16:9 |
| Internal resolution | 1x (`PS2X_HW_SCALE=1`) | the window's (0) |
| MSAA / anisotropic | off / off | 4x / 8x |
| PS2 blur filter | on | off |
| HUD | Keep 4:3, the original textures | Screen edges, Sharp HUD, the HD pack (copied from the player's data folder) |
| Frame rate | 30 | 60 |
