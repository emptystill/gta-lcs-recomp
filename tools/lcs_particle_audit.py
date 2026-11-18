#!/usr/bin/env python3
"""Liberty City Stories: particles per simulated second at two forced time steps (docs/LCS.md "Above 30 fps").

    python tools/lcs_particle_audit.py [OUT] [--steps 30,144] [--env NAME=VALUE ...]

Each step is a hidden run from a new game (its own data folder) in frame-rate independent timing with VCS_TIMESTEP=N
(the game unpaced, 1/N s of game time a frame) and VCS_PARTICLE_LOG=1. Once in play (the intro over): god mode, noon, an
Idaho warped into, then scenarios timed on the game clock - idle, driving, a burnout, rain idling, out in the rain. The
log's [particles] lines (AddParticle calls per second by return address, passed by the timing; lifetimes by type) are
summed per run, and the second step is compared with the first: the calls that pass per call site, and how long each
particle type lives. With src/game/vcs_particle_timing.cpp on, both should match the 30 fps run's within the
scenarios' own variation; --env VCS_PARTICLE_TIMING=0 shows the before.

Writes OUT/<step>/rates.json and OUT/report.txt (OUT: build/audit/particles-<date-time>).
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamectl  # noqa: E402
import toolchain as tc  # noqa: E402

CLOCK = 0x3D9B70  # CTimer::m_snTimeInMilliseconds (LCS)


def send(c):
    try:
        return gamectl.send(c, timeout=30)
    except Exception as e:  # noqa: BLE001 - a dead game answers nothing
        return "error %s" % e


def clock():
    r = send("peek 0x%x 4" % CLOCK)
    return int.from_bytes(bytes.fromhex(r[3:].strip()), "little") if r.startswith("ok ") else 0


def wait_game(ms):
    start, t0 = clock(), time.time()
    while clock() - start < ms and time.time() - t0 < 600:
        time.sleep(0.2)


def pick(listing, *names):
    for n in names:
        m = re.search(r" (\d+):([^;]*%s[^;]*);" % n, listing, re.I)
        if m:
            return m.group(1)
    return None


def run(out, step, extra):
    name = os.path.basename(out.rstrip("\\/")) + "-" + step
    data = os.path.join(out, step)
    os.makedirs(data, exist_ok=True)
    env = dict(os.environ, VCS_DATA_DIR=os.path.join(data, "data"), PS2X_HIDDEN_WINDOW="1280x720", VCS_FULLSCREEN="0",
               VCS_PARTICLE_LOG="1", VCS_TIMESTEP=step, VCS_TIMING="1", VCS_FPS="0", **extra)
    log = os.path.join(tc.ROOT, "build", "run", name + ".log")
    game = subprocess.Popen([sys.executable, "-I", os.path.join(tc.ROOT, "tools", "run.py"), "1500", name, "--hidden"],
                            cwd=tc.ROOT, env=env)
    gamectl.wait(timeout=120)
    t0 = time.time()
    while time.time() - t0 < 900:  # the intro plays out (~260 s of game time)
        if "ready=1" in send("trainer state") and clock() > 260000:
            break
        time.sleep(2)
    wait_game(5000)
    for c in ("trainer toggle god 1", "trainer toggle warp 1", "trainer toggle wanted 1", "trainer time 12"):
        send(c)
    car = pick(send("trainer list vehicles"), "Idaho", "Kuruma", "Sentinel") or "0"
    send("trainer spawn " + car)
    wait_game(13000)                                                                      # idle
    send("key w down"); wait_game(6000); send("key w up"); wait_game(2000)               # driving
    send("key w down"); send("key s down"); wait_game(5000)                              # a burnout
    send("key s up"); send("key w up"); wait_game(2000)
    send("trainer weather %s" % (pick(send("trainer list weathers"), "rain", "lluv") or "2")); wait_game(10000)
    send("press f 300"); wait_game(3000); send("key w down"); wait_game(6000); send("key w up")  # out in the rain
    wait_game(12000)
    send("quit")
    game.wait(timeout=120)
    calls, passed, life, secs = {}, {}, {}, 0.0
    for line in open(log, encoding="utf-8", errors="replace"):
        m = re.match(r"\[particles\] ([\d.]+) s, [\d.]+ frames/s, AddParticle calls/s by return address \(passed\):(.*)", line)
        if m:
            s = float(m.group(1))
            secs += s
            for ra, c, p in re.findall(r" ([0-9a-f]+):([\d.]+)\(([\d.]+)\)\*?", m.group(2)):
                calls[ra] = calls.get(ra, 0.0) + float(c) * s
                passed[ra] = passed.get(ra, 0.0) + float(p) * s
        elif line.startswith("[particles] lifetimes"):
            for t, d, ms in re.findall(r" (\d+):([\d.]+)/(\d+)", line):
                n, tot = life.get(t, (0.0, 0.0))
                life[t] = (n + float(d), tot + float(d) * float(ms))
    result = {"secs": secs, "calls": calls, "passed": passed, "life": life}
    json.dump(result, open(os.path.join(data, "rates.json"), "w"), indent=1)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", nargs="?")
    ap.add_argument("--steps", default="30,144")
    ap.add_argument("--env", nargs="*", default=[])
    a = ap.parse_args()
    out = os.path.abspath(a.out or os.path.join(tc.ROOT, "build", "audit",
                                                "particles-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S")))
    extra = dict(e.split("=", 1) for e in a.env)
    steps = a.steps.split(",")
    res = {s: run(out, s, extra) for s in steps}
    base, other = res[steps[0]], res[steps[-1]]
    lines = ["AddParticle calls passed per simulated second, by return address (%s vs %s fps):" % (steps[0], steps[-1])]
    for ra in sorted(set(base["passed"]) | set(other["passed"]), key=lambda x: int(x, 16)):
        b = base["passed"].get(ra, 0.0) / max(base["secs"], 1)
        o = other["passed"].get(ra, 0.0) / max(other["secs"], 1)
        lines.append("  %8s %9.2f %9.2f  x%.2f" % (ra, b, o, o / b if b else 0.0))
    lines.append("Lifetimes by particle type, mean ms (%s vs %s fps):" % (steps[0], steps[-1]))
    for t in sorted(set(base["life"]) | set(other["life"]), key=int):
        bn, bt = base["life"].get(t, (0, 0))
        on, ot = other["life"].get(t, (0, 0))
        lines.append("  type %3s %8.0f %8.0f" % (t, bt / bn if bn else 0, ot / on if on else 0))
    open(os.path.join(out, "report.txt"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))
    print("report: " + os.path.join(out, "report.txt"))


if __name__ == "__main__":
    main()
