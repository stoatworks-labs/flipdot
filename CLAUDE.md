# flipdot

The picture on an electromagnetic flip-dot sign, as an FFGL **effect** for
Resolume Arena/Avenue. C++/GLSL, CMake MODULE → universal `.bundle` (macOS) +
Windows `.dll`. MIT. Intended home `github.com/stoatworks-labs/flipdot`.

Read `AGENTS.md` before changing the driver, the disc model, the dither or the
parameter list.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64` (use a different
  directory, e.g. `build-dev`; `tools/verify.sh` deletes and rebuilds `build`)
- Build: `cmake --build build`
- Install into Arena: `cmake --install build` → `~/Documents/Resolume Arena/Extra Effects`
  (untested; never run by this repo's own workflow)
- Render a frame offline: `./build/fdtest --out f.png --size 1920x1080 --frames 120`
  (`--drift 2` walks the test card sideways so the sign has something to chase)
- Set anything by name: `--set "Columns=96" --set "Dither=2" --set "Light=1"`
  (options by index: Layout 0 Square, 1 Offset; Colour 0 Yellow, 1 Green,
  2 White; Scan 0 Column by Column, 1 Row by Row; Update 0 Continuous,
  1 Interval, 2 Onset, 3 Manual; Dither 0 Threshold, 1 Bayer 4x4,
  2 Floyd-Steinberg)
- Frames for video: `./build/fdtest --pipe --size 1920x1080 --fps 60 --script cues.txt | ffmpeg -f rawvideo -pix_fmt rgba -s 1920x1080 -r 60 -i - out.mov`
  — a cue script is `frame  Parameter Name  value`; standard parameters ramp
  between keys, options/booleans/integers/events STEP. A reader that hangs up
  gets exit 1 (SIGPIPE is ignored). With nothing on stdin and `--frames N` it
  films the test card.
- List parameters: `./build/fdtest --list` (prints the real range of an option)
- Demo clips: `tools/clips.sh [out-dir]` (needs ffmpeg and a Resolume install)

## Verify
- Everything: `tools/verify.sh` (fresh universal build + every check on both
  renderers + two sweeps; bash, not zsh). Logs go to `$TMPDIR/flipdot-verify`
  (or `$FLIPDOT_LOGS`).
- **The checks, each read off the rendered picture at 640x360 and 320x180:**
  - `./build/fdtest --wipe` — line L of a whole-sign change lands on frame ceil(2.4 L + 6.5), exactly; by column, by row, and on Resolume's 499,000,000 ms clock
  - `./build/fdtest --changes` — Only Changes moves exactly the discs whose target differs; Refresh All moves every disc and the unchanged ones come back bit-identical
  - `./build/fdtest --bistable` — no update, no change: the settled sign bit-identical, with the clip frozen, inverted, or the driver sweeping it
  - `./build/fdtest --stuck` — exactly round(f × N) discs never move, they are the seeded set, and 0.2's set holds 0.1's
  - `./build/fdtest --rotation` — the colour face's width against 2r|cos θ| from the harness's own integration of the torques, within 1.1 px
  - `./build/fdtest --resize` — a picture resize mid-pass and a 16 → 32 column regrid keep every disc's side
  - `./build/fdtest --prime` — loud audio on frame one, and after a retrigger, fires nothing; a real onset fires
  - `./build/fdtest --negative` — every check against its broken model; each must fail
  - `./build/fdtest --names` — no name over 16 characters, none duplicated
  - `FDTEST_RENDERER=software ./build/fdtest --wipe` — any check on Apple's software renderer (CI's fallback)
- Cost: `./build/fdtest --bench`
- No dead controls: `python3 tools/sweep.py` (`--size WxH`, `--jobs N`, `--binary PATH`)
- The mutation test: `tools/mutate.sh` (one character of the shipped GLSL or the model; a few minutes)

## Notes
- **The sign lives on the CPU** (`Sign.cpp`): every disc's side, motion and
  age, the driver's pass, in double. The GPU copies the clip, takes each
  disc's mean luma (read back), and draws each disc from its cos and sin. A
  wrong count or a wrong frame is a `Sign.cpp` fix; a wrong pixel is a
  board-shader fix.
- **Time never reaches anything as an absolute.** The `Clock` settles the
  host's unit; only the frame's delta, clamped to 0.25 s, goes into the sign.
  Line L of a pass is visited L / Scan Rate after it started, to the double,
  inside whatever frame that falls in.
- **The shader does no trigonometry.** cos and sin of each disc's angle and of
  the light are computed on the CPU in double: Apple's software renderer's
  `cos` is out by 1e-3 near edge-on.
- **The disc profile is stated, in `Disc.h`**: θ = π u² to the far stop, then
  rebounds of e^k flip units rising π e^(2k)/2, cut below half a degree.
  `--rotation` integrates the same torques itself rather than reading the
  closed form.
- **Threshold is the luma that lights half the discs**, a tone curve through
  (0,0), (T,0.5), (1,1), for all three dithers; black stays black.
- **The sign is bistable across a clip trigger.** `InitGL` does not reset it;
  only a new instance starts blank.
- **Onset and Manual do not fire on the first frame**; Continuous and Interval
  pick the clip up at once. The onset detector primes on frame one.
- **Options map by index in `Controls.cpp` (`OptionIndex`)**; the SDK's range
  for an option reads back 0..1 whatever the element count, and `--list`
  prints the real one for the sweep.
- **GLSL reserved words**: `patch sample input output filter common active half
  layout flat packed`, and GLSL 4.10's reserved list (`external`, `interface`,
  `fixed`...). The Layout control is `OffsetRows` in the shaders.
- `SetParamInfo` clamps a STANDARD default into 0..1; Columns and Rows are
  `FF_TYPE_INTEGER`, which is exempt.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no
  host can instantiate the plugin.
- `flipdot_core` is an OBJECT library, not STATIC.
- macOS build must be universal. Verify with `lipo`, never the build log.
- FFGL id is `FD01`. Display name `SW Flipdot`. Bundle id `com.stoatworks.ffgl.flipdot`.
- `StoatworksAbout.h` and `ATTRIBUTIONS.md` are PROVISIONAL hand copies with
  `guide = ""` (no user guide yet), so the About block is four entries (text
  plus three buttons) and `Flipdot.cpp`'s `static_assert` holds that to
  `about::kParamCount`. Registering the project and syncing will add a fourth
  button: add `PT_ABOUT_BUTTON_4` then.

## Not done yet
- Never loaded into Resolume; no real audio has reached it in a host. CI is
  written and has not run.
- No presets, no OpenFX port, no browser demo, no user guide.

## Diagnostics

`source/Diag.{h,cpp}` — log file only, no crash handler (this runs inside
Resolume). It records the GL driver, which shader failed, and whether audio
reached the layer.

    ~/Library/Logs/flipdot/flipdot.YYYY-MM-DD.log
