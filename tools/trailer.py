#!/usr/bin/env python3
"""Trailer footage: scripted shots recorded from the game as video files (one game frame per video frame).

    python tools/trailer.py list                         the shots
    python tools/trailer.py shoot [SHOT ...] [--out DIR] [--size 2560x1440] [--save DIR] [--keep-going]
    python tools/trailer.py look SHOT                    the shot's first frame only, as a PNG (framing a shot quickly)

Each shot is a hidden game run of its own: a copy of a memory card (default build/rig/saves/user: a copy of the
player's own card, never the card itself) loads its newest save; the trainer sets the place, hour, weather, weapons,
vehicle and wanted level; then "record start" (the runtime's recorder: every new picture piped to ffmpeg, the game
held to the encoder's pace) runs while the shot's actions play on the game clock. The game runs with a forced time
step (VCS_TIMESTEP = the video's frame rate), so motion is exact whatever the encoding speed, and silent (the music
goes on in the editor).

Two looks: "ps2" - 4:3, the PS2's resolution and blur, its HUD, 30 fps - and "pc" - widescreen, the window's
resolution, MSAA, sharp / HD HUD at the screen's edges, 60 fps. Videos go to build/trailer/<shot>.mp4 (H.264, NVENC;
PS2X_RECORD_CODEC replaces the codec). Needs ffmpeg on PATH.

A shot's actions are (game seconds after the recording starts, command): any control-channel command ("key w down",
"mouse vel 3 0", "trainer boost") or one of the shot verbs below:
    burn          the nearest car spawned for the shot set on fire (its health to 0: it burns, then explodes)
    stop          the recording ends here (otherwise at the shot's length)
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamectl  # noqa: E402
import rig  # noqa: E402
import toolchain as tc  # noqa: E402

CLOCK = 0x3D9B70        # CTimer::m_snTimeInMilliseconds (stands still while paused)
PLAYER_PED = 0x408E40   # the player's CPed
PED_VEHICLE = 0x534     # CPed: the vehicle he is in (LCS)
VEH_HEALTH = 0x268      # CVehicle: health (0: it burns and explodes)

COMMON_ENV = {"VCS_FULLSCREEN": "0", "VCS_TIMING": "1", "VCS_FPS": "0", "LCS_VOLUME": "0", "VCS_SKIP_MOVIES": "1",
              "LCS_F2_MENU": "0", "VCS_PERF_OVERLAY": "0", "PS2X_FRAME_STATS": "0",
              "LCS_TRAINER_NOTICES": "0"}
LOOKS = {
    "ps2": {"fps": 30, "env": {"VCS_ASPECT": "43", "VCS_HUD": "0", "PS2X_HW_SCALE": "1", "PS2X_HW_MSAA": "1",
                               "PS2X_HW_ANISO": "0", "LCS_PS2_BLUR": "1", "LCS_HUD_SHARP": "0", "LCS_HUD_PACK": "0"}},
    "pc": {"fps": 60, "env": {"VCS_ASPECT": "169", "VCS_HUD": "2", "PS2X_HW_SCALE": "0", "PS2X_HW_MSAA": "4",
                              "PS2X_HW_ANISO": "8", "LCS_PS2_BLUR": "0", "LCS_HUD_SHARP": "1", "LCS_HUD_PACK": "1"}},
}

# The shots of docs/TRAILER.md (the script plan). place: a trainer landmark (a word of its name); vehicle: a word of a
# vehicle's name; road: try eight headings first and drive the one that goes furthest; seconds: game seconds recorded.
# start: (x, y, z, heading) - the player (or his car) put there, facing the heading (z -100: the ground); the same
# start for 01-03 makes the 4:3 -> widescreen cut land on the same street. street: on foot on a road (from traffic),
# the camera looking along it. hud: False hides the HUD (vistas).
SAINT_MARKS_ROAD = (1196.0, -264.0, -100.0, -2.0)
SHOTS = {
    "01_ps2_street": dict(look="ps2", place="Saint Mark", start=SAINT_MARKS_ROAD, hour=13, weather="Sunny", seconds=7,
                          actions=[(0, "key w down"), (0, "mouse vel 0.6 0")]),
    "02_ps2_drive": dict(look="ps2", place="Saint Mark", start=SAINT_MARKS_ROAD, hour=13, weather="Sunny",
                         vehicle="Banshee|Stinger", seconds=6, actions=[(0, "key w down")]),
    "03_pc_drive": dict(look="pc", place="Saint Mark", start=SAINT_MARKS_ROAD, hour=13, weather="Sunny",
                        vehicle="Banshee|Stinger", seconds=6, actions=[(0, "key w down")]),
    "04_pc_sunset_drive": dict(look="pc", place="Callahan", hour=19, weather="Extra sunny", vehicle="Banshee|Stinger",
                               road=True, seconds=8, actions=[(0, "key w down"), (1, "mouse vel -1.2 0")]),
    "05_pc_night_rain": dict(look="pc", place="Red Light", hour=23, weather="Rainy", vehicle="Banshee|Stinger",
                             road=True, seconds=8, actions=[(0, "key w down"), (0.5, "mouse vel 1.0 0")]),
    "06_pc_mouse_aim": dict(look="pc", place="Harwood", hour=21, weather="Rainy", weapons=[32], targets=2, seconds=8,
                            actions=[(0, "key mouseright down"), (0.5, "key mouseleft down"), (0, "mouse vel 0.5 0"),
                                     (3.0, "mouse vel -0.9 0"), (6.5, "key mouseleft up")]),
    "07_pc_explosions": dict(look="pc", place="Callahan", hour=20, weather="Sunny", weapons=[30], targets=3, seconds=11,
                             actions=[(0.3, "burn"), (1.5, "burn"), (2.7, "burn"), (0, "mouse vel 1.2 0")]),
    "08_pc_police": dict(look="pc", place="Chinatown", hour=22, weather="Cloudy", vehicle="Banshee|Stinger", road=True,
                         wanted=4, seconds=10, actions=[(0, "key w down"), (0, "mouse vel -0.8 0")]),
    "09_pc_vista": dict(look="pc", place="Fort Staunton", street=True, hud=False, hour=6, weather="Extra sunny",
                        seconds=10, actions=[(0, "mouse vel 1.2 0")]),
    "10_pc_night_vista": dict(look="pc", place="Bedford", street=True, hud=False, hour=0, weather="Sunny", seconds=10,
                              actions=[(0, "mouse vel -1.0 0")]),
}


def send(cmd, timeout=30):
    try:
        return gamectl.send(cmd, timeout=timeout)
    except Exception as e:  # noqa: BLE001 (a command that failed is logged, the shot goes on)
        return "error %s" % e


def peek32(addr):
    r = send("peek 0x%x 4" % addr, timeout=10)
    return int.from_bytes(bytes.fromhex(r[3:].strip()), "little") if r.startswith("ok ") else 0


def clock():
    return peek32(CLOCK)


def wait_game(ms, limit=120):
    start, t0 = clock(), time.time()
    while clock() - start < ms and time.time() - t0 < limit:
        time.sleep(0.05)


def position():
    """The player's position (his ped follows the vehicle he drives)."""
    ped = peek32(PLAYER_PED)
    if not ped:
        return None
    r = send("peekf 0x%x 3" % (ped + 0x30), timeout=10)
    w = r.split()
    return tuple(float(v) for v in w[1:4]) if len(w) >= 4 and w[0] == "ok" else None


def pick(listing, pattern):
    """The id of the first "N:name;" entry whose name matches `pattern` (case-insensitive)."""
    for m in re.finditer(r" (\d+):([^;]*);", listing):
        if re.search(pattern, m.group(2), re.I):
            return m.group(1)
    return None


def find_road(log):
    """Put the player's car on a road, facing the way traffic drives: 20 m ahead of a traffic car seen driving nearby
    (trainer roadcar); without one, the heading (of eight) the car goes furthest along from here in 2.5 game seconds.
    Returns the heading (degrees)."""
    import math
    for _ in range(12):
        r = send("trainer roadcar")
        if r.startswith("ok "):
            x, y, z, h, v = (float(t) for t in r[3:].split())
            a = math.radians(h)
            send("trainer goto %.2f %.2f %.2f" % (x - math.sin(a) * 20.0, y + math.cos(a) * 20.0, z + 0.5))
            wait_game(300)
            send("trainer heading %.1f" % h)
            send("trainer repair")
            wait_game(800)
            log("  road from traffic: (%.0f, %.0f) heading %.0f" % (x, y, h))
            return h
        wait_game(1000)
    send("trainer savepos")
    best, best_d = 0, -1.0
    for heading in range(0, 360, 45):
        send("trainer returnpos")
        wait_game(400)
        send("trainer heading %d" % heading)
        wait_game(300)
        a = position()
        send("key w down")
        wait_game(2500)
        send("key w up")
        b = position()
        d = ((b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2) ** 0.5 if a and b else 0.0
        log("  heading %3d: %.1f m" % (heading, d))
        if d > best_d:
            best, best_d = heading, d
    send("trainer returnpos")
    wait_game(500)
    send("trainer heading %d" % best)
    send("trainer repair")
    wait_game(800)
    return best


def street(log):
    """On foot on a road: 20 m ahead of a traffic car seen driving, the camera to look along the road. The heading."""
    import math
    for _ in range(12):
        r = send("trainer roadcar")
        if r.startswith("ok "):
            x, y, z, h, _v = (float(t) for t in r[3:].split())
            a = math.radians(h)
            send("trainer goto %.2f %.2f %.2f" % (x - math.sin(a) * 20.0, y + math.cos(a) * 20.0, z + 0.5))
            wait_game(1500)
            log("  street from traffic: (%.0f, %.0f) heading %.0f" % (x, y, h))
            return h
        wait_game(1000)
    log("  no traffic seen: the camera stays")
    return None


def shoot(name, spec, out_dir, size, save, log, extra_env=None):
    look = LOOKS[spec["look"]]
    run_dir = os.path.join(tc.ROOT, "build", "trailer", "runs", name)
    shutil.rmtree(run_dir, ignore_errors=True)
    data = os.path.join(run_dir, "data")
    rig.copy_card(os.path.join(save, "mc0"), os.path.join(data, "mc0"))
    pack = os.path.join(rig_data_root(), "hud_pack")  # the player's HD HUD pack (read only, copied)
    if os.path.isdir(pack) and look["env"].get("LCS_HUD_PACK") == "1":
        shutil.copytree(pack, os.path.join(data, "hud_pack"))
    env = dict(os.environ, VCS_DATA_DIR=data, PS2X_HIDDEN_WINDOW=size, VCS_TIMESTEP=str(look["fps"]))
    env.update(COMMON_ENV)
    env.update(look["env"])
    if spec.get("vehicle"):
        env["LCS_CAR_RETURN"] = "10"  # behind the car again 1 s after the shot's last mouse turn
    env.update(extra_env or {})
    game = subprocess.Popen([sys.executable, "-I", tc.tool("run.py"), "1800", "trailer_" + name, "--hidden"],
                            cwd=tc.ROOT, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    try:
        gamectl.wait(timeout=120)
        t0 = time.time()
        while time.time() - t0 < 300:  # the save loaded: the trainer ready and the clock running
            if "ready=1" in send("trainer state"):
                c = clock()
                time.sleep(1.0)
                if clock() > c:
                    break
            time.sleep(1.0)
        else:
            raise RuntimeError("not in game after 300 s")
        wait_game(2000)
        for c in ("trainer toggle god 1", "trainer toggle ammo 1", "trainer toggle warp 1", "trainer setwanted 0"):
            send(c)
        weathers = send("trainer list weathers")
        if spec.get("weather"):
            send("trainer weather %s" % (pick(weathers, spec["weather"]) or "0"))
        if spec.get("place"):
            where = pick(send("trainer list landmarks"), spec["place"])
            log("  teleport %s: %s" % (spec["place"], send("trainer teleport %s" % where)))
            wait_game(6000)  # the area streamed in, the zone's name the game shows gone
        if spec.get("hud") is False:
            send("trainer toggle hidehud 1")
        heading = None
        if spec.get("start") and not spec.get("vehicle"):
            x, y, z, heading = spec["start"]
            send("trainer goto %.2f %.2f %.2f" % (x, y, z))
            wait_game(1500)
        if spec.get("street"):
            heading = street(log)
        send("trainer time %d" % spec.get("hour", 12))
        for weapon in spec.get("weapons", []):
            send("trainer weapon %d" % weapon)
        vehicles = send("trainer list vehicles")
        targets = []
        if spec.get("targets"):  # cars to shoot / burn: each 5 m ahead of the player, who steps sideways between them
            home = position()
            send("trainer toggle warp 0")
            for k in range(spec["targets"]):
                side = (k - (spec["targets"] - 1) / 2.0) * 7.0
                send("trainer goto %.2f %.2f %.2f" % (home[0] + side, home[1], home[2]))
                wait_game(300)
                send("trainer spawn %s" % (pick(vehicles, "Stallion|Idaho|Esperanto|Kuruma") or "92"))
                wait_game(800)
            send("trainer goto %.2f %.2f %.2f" % home)
            wait_game(500)
            targets = nearby_cars()
            log("  targets %d" % len(targets))
            send("trainer toggle warp 1")
        if spec.get("vehicle"):
            vid = pick(vehicles, spec["vehicle"])
            log("  vehicle %s: %s" % (spec["vehicle"], send("trainer spawn %s" % vid)))
            send("trainer toggle vehicle 1")  # invincible: the police's bullets must not end the shot
            wait_game(1500)
            if spec.get("start"):
                x, y, z, h = spec["start"]
                send("trainer goto %.2f %.2f %.2f" % (x, y, z))
                wait_game(500)
                send("trainer heading %.1f" % h)
                wait_game(800)
            elif spec.get("road"):
                log("  road heading %d" % find_road(log))
        if spec.get("wanted"):
            send("trainer setwanted %d" % spec["wanted"])
        send("camera heading %.1f" % heading if heading is not None else "camera behind")
        wait_game(8500 if spec.get("vehicle") else 1500)  # the vehicle's name the game shows on getting in: gone
        video = os.path.join(out_dir, name + ".mp4")
        r = send("record start %s %d" % (video, look["fps"]))
        log("  record %s" % r)
        start = clock()
        actions = sorted(spec.get("actions", []), key=lambda a: a[0])
        i = 0
        length = spec["seconds"] * 1000
        wall = time.time()
        while clock() - start < length and time.time() - wall < 900:
            now = clock() - start
            while i < len(actions) and actions[i][0] * 1000 <= now:
                cmd = actions[i][1]
                i += 1
                if cmd == "stop":
                    length = 0
                elif cmd == "burn":
                    if targets:
                        car = targets.pop(0)
                        send("poke 0x%x %s" % (car + VEH_HEALTH, struct.pack("<f", 0.0).hex()))
                else:
                    send(cmd)
            time.sleep(0.01)
        log("  stop %s (%.0f s)" % (send("record stop"), time.time() - wall))
        send("mouse vel 0 0")
        return video
    finally:
        send("quit")
        try:
            game.wait(timeout=60)
        except subprocess.TimeoutExpired:
            if tc.IS_WINDOWS:
                subprocess.run(["taskkill", "/T", "/F", "/PID", str(game.pid)], capture_output=True)


def nearby_cars():
    """The vehicles in the pool nearest the player first (addresses), from the rig's gameinfo-free peek of the pool."""
    r = send("trainer nearcars")
    if r.startswith("ok "):
        return [int(x, 16) for x in r[3:].split()]
    return []


def rig_data_root():
    import backup  # noqa: E402 (the game's own data folder, for the HUD pack)
    saved = os.environ.pop("VCS_DATA_DIR", None)
    try:
        return backup.data_dir()
    finally:
        if saved is not None:
            os.environ["VCS_DATA_DIR"] = saved


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("verb", choices=["list", "shoot"])
    ap.add_argument("shots", nargs="*")
    ap.add_argument("--out", default=os.path.join(tc.ROOT, "build", "trailer"))
    ap.add_argument("--size", default="2560x1440")
    ap.add_argument("--save", default=os.path.join(tc.ROOT, "build", "rig", "saves", "user"))
    ap.add_argument("--keep-going", action="store_true")
    ap.add_argument("--env", action="append", default=[], help="K=V after the look's settings (experiments)")
    a = ap.parse_args()
    if a.verb == "list":
        for name, s in SHOTS.items():
            print("%-22s %-3s %4.0f s  %s" % (name, s["look"], s["seconds"], s.get("place", "")))
        return
    if not shutil.which("ffmpeg"):
        tc.fail("ffmpeg not found on PATH (the recorder pipes the pictures to it)", "missing_tool")
    if not os.path.isdir(os.path.join(a.save, "mc0")):
        tc.fail("no memory card copy in %s (copy your card's mc0 there first)" % a.save, "error")
    os.makedirs(a.out, exist_ok=True)
    names = a.shots or list(SHOTS)
    for name in names:
        if name not in SHOTS:
            tc.fail("no shot %s (python tools/trailer.py list)" % name, "error")
    for name in names:
        print("[trailer] %s" % name, flush=True)
        try:
            video = shoot(name, SHOTS[name], a.out, a.size, a.save, lambda m: print(m, flush=True),
                          dict(e.split("=", 1) for e in a.env))
            print("[trailer] %s -> %s" % (name, video), flush=True)
        except Exception as e:  # noqa: BLE001
            print("[trailer] %s failed: %s" % (name, e), flush=True)
            if not a.keep_going:
                raise


if __name__ == "__main__":
    main()
