"""Every parameter must actually change the picture.

A uniform name that does not match between the C++ and the GLSL is silently
ignored: glGetUniformLocation returns -1, glUniform on -1 is a documented no-op,
and nothing in the build says a word. A setting the CPU reads but never acts on
is quieter still. A control can therefore be completely dead while everything
compiles, links, loads and renders. Nothing else in this repo catches that.

So: render each parameter at both ends of its range against a context that makes
it mean something, and report any that made no difference at all.

    python3 tools/sweep.py [--size WxH] [--jobs N] [--binary PATH]

Exit code 1 means something is dead.

------------------------------------------------------------------ the traps

**A still picture settles, and then the driver means nothing.** Continuous,
Interval and a sweep of a still card all end on the same picture. The Update
controls are swept with `--drift`, which walks the test card sideways a pixel
or two a frame, so the sign has something to chase.

**An event is a press on a frame, not a value.** Manual mode does not fire on
the first frame, so Update Now is swept with a cue script that presses it on
frame 60, in Manual mode, over a drifting card: pressed, the sign shows the
card; not, it is blank.

**The swing, the rebound and the late discs are over in tens of milliseconds.**
Each is swept at a frame chosen inside its motion, with the whole sign pulsed
at once (Scan Rate at the top of its range) so that frame is the same for
every disc.

**Scan and Scan Rate only change the order and the timing**, so they are swept
a third of the way through the first sweep.

**Options are swept by index**, which `fdtest --list` prints as the real range;
the SDK's own range for an option reads back 0..1 whatever its element count.

**Never sweep the About block.** Those are buttons that open a web browser.
"""
import argparse
import concurrent.futures
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
BIN = str(ROOT / "build" / "fdtest")
SCRATCH = tempfile.mkdtemp(prefix="fdsweep")

WIDTH, HEIGHT = 640, 360
FRAMES = 120          # two seconds at 60 fps

# `_frames`, `_drift`, `_low`, `_high` and `_press` are harness or sweep
# settings, not parameters; anything starting with an underscore is stripped
# before --set.
MOVING = {"_drift": 2, "_frames": 120}

CONTEXT = {
    "Layout": {"_high": 1},
    "Colour": {"_high": 2},
    # Row by row and column by column settle on the same picture; swept
    # mid-sweep, a third of a second in at 20 lines a second.
    "Scan": {"Scan Rate": 0.233, "_frames": 20},
    "Scan Rate": {"_frames": 20},
    "Update": dict(MOVING, _high=3),
    "Interval": dict(MOVING, Update=1),
    "Update Now": dict(MOVING, Update=3, _press=60),
    # Refresh All kicks every disc the sweep reaches; on a still card with
    # Only Changes nothing moves once it has settled.
    "Only Changes": dict(MOVING),
    # The whole sign pulsed in a frame (4000 lines a second), three frames
    # in: 5 ms a swing has landed, 1 s is barely under way.
    "Flip Time": {"Scan Rate": 1.0, "_frames": 3},
    # Half a second a swing: landed on frame 30, five frames into the rebound.
    "Rebound": {"Scan Rate": 1.0, "Flip Time": 0.8692, "_frames": 35},
    # A weak coil takes 1.5 to 3 swing times: at frame 35 of a 30-frame
    # swing the sound discs have landed and the weak ones are in the air.
    "Late": {"Scan Rate": 1.0, "Flip Time": 0.8692, "Rebound": 0.0, "_frames": 35},
    "Dither": {"_high": 2},
}


def parameters():
    """id, name, kind, low, high from the harness's own declaration."""
    out = subprocess.run([BIN, "--list"], capture_output=True, text=True)
    if out.returncode != 0:
        print("could not list parameters:", out.stdout, out.stderr)
        sys.exit(1)

    found = []
    for line in out.stdout.splitlines():
        m = re.match(
            r"\s*(\d+)\s+(.+?)\s{2,}(\S+)\s+([\d.eE+-]+)\s+\[\s*([\d.eE+-]+)\s*\.\.\s*([\d.eE+-]+)\s*\]",
            line,
        )
        if m:
            found.append(
                (int(m.group(1)), m.group(2).strip(), m.group(3),
                 float(m.group(5)), float(m.group(6)))
            )
        else:
            m = re.match(r"\s*(\d+)\s+(.+?)\s{2,}(about|text|buffer)\s", line)
            if m:
                found.append((int(m.group(1)), m.group(2).strip(), m.group(3), 0.0, 0.0))
    return found


def render(path, overrides):
    frames = overrides.get("_frames", FRAMES)
    args = [BIN, "--out", path, "--size", f"{WIDTH}x{HEIGHT}",
            "--frames", str(frames), "--fps", "60"]
    if "_drift" in overrides:
        args += ["--drift", str(overrides["_drift"])]
    if "_script" in overrides:
        args += ["--script", overrides["_script"]]
    for name, value in overrides.items():
        if not name.startswith("_"):
            args += ["--set", f"{name}={value}"]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        print("render failed:", " ".join(args), r.stdout, r.stderr)
        sys.exit(1)
    return pathlib.Path(path).read_bytes()


def pixels(png):
    """Raw RGBA out of the harness's own PNG (filter 0 rows), so nothing else
    is a dependency."""
    i = 8
    idat = b""
    width = height = 0
    while i < len(png):
        length = int.from_bytes(png[i:i + 4], "big")
        kind = png[i + 4:i + 8]
        data = png[i + 8:i + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(data[0:4], "big")
            height = int.from_bytes(data[4:8], "big")
        elif kind == b"IDAT":
            idat += data
        i += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for row in range(height):
        out += raw[row * (stride + 1) + 1:(row + 1) * (stride + 1)]
    return out


def difference(a, b):
    pa, pb = pixels(a), pixels(b)
    if len(pa) != len(pb):
        return 1.0, len(pa)
    changed = sum(1 for x, y in zip(pa, pb) if x != y)
    return changed / max(len(pa), 1), changed


def sweep_one(job):
    pid, name, kind, low, high, context = job

    lo = dict(context)
    hi = dict(context)
    if kind == "event":
        # A press on a frame, by cue script; the low side never presses.
        cue = pathlib.Path(SCRATCH) / f"{pid}_press.txt"
        cue.write_text(f"{context.get('_press', 60)}  {name}  1\n")
        hi["_script"] = str(cue)
    else:
        lo[name] = context.get("_low", low)
        hi[name] = context.get("_high", high)

    a = render(f"{SCRATCH}/{pid}_lo.png", lo)
    b = render(f"{SCRATCH}/{pid}_hi.png", hi)
    fraction, count = difference(a, b)
    print(f"  swept {pid:3d} {name}", file=sys.stderr, flush=True)
    return pid, name, fraction, count


def main():
    global WIDTH, HEIGHT, BIN

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--size", default="%dx%d" % (WIDTH, HEIGHT))
    ap.add_argument("--jobs", type=int, default=0)
    ap.add_argument("--binary", default=BIN)
    args = ap.parse_args()
    if "x" in args.size:
        WIDTH, HEIGHT = (int(v) for v in args.size.split("x", 1))
    BIN = args.binary
    jobs = args.jobs or min(8, os.cpu_count() or 1)

    if not pathlib.Path(BIN).exists():
        print(f"{BIN} is not built")
        return 1

    skipped = []
    work = []
    for pid, name, kind, low, high in parameters():
        if kind == "about":
            skipped.append((name, "a button that opens a web browser"))
            continue
        if kind == "buffer":
            skipped.append((name, "the host's spectrum; --prime exercises it"))
            continue
        work.append((pid, name, kind, low, high, CONTEXT.get(name, {})))

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for r in pool.map(sweep_one, work):
            results.append(r)

    dead = []
    for pid, name, fraction, count in sorted(results):
        if count == 0:
            dead.append(name)
            print(f"DEAD  {pid:4d}  {name}")
        else:
            print(f"ok    {pid:4d}  {name}  ({count} subpixels, {fraction * 100:.2f}%)")

    print()
    for name, why in skipped:
        print(f"skip  {name}: {why}")

    print(f"\n{len(results)} swept at {WIDTH}x{HEIGHT}, {len(dead)} dead, {len(skipped)} skipped, {jobs} at a time")
    if dead:
        print("\nDEAD CONTROLS: " + ", ".join(dead))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
