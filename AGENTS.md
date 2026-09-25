# flipdot — orientation for another LLM (or a newcomer)

**What it is:** an FFGL 2.1 **effect** for Resolume Arena/Avenue that shows
the clip on an electromagnetic flip-dot sign: a grid of bistable discs, black
on one side and fluorescent on the other, swung by coil pulses from a driver
board that scans a column (or a row) at a time and pulses only what changes.
C++17 + GLSL 4.10, CMake, universal macOS `.bundle` (a Windows `.dll` is
configured and has never been built). MIT. Intended home
`github.com/stoatworks-labs/flipdot`; built 2026-09-25 as a local v0.1.0 in
`~/dev/flipdot`, tranche five, Allan's own pick. **Never loaded into
Resolume.**

`CLAUDE.md` is the command reference. This file is the *why*: the idea, every
number in the harness and where its tolerance comes from, whether each would
hold on another rasteriser, the negative controls, the mutation test, the traps
this build actually hit, the decisions taken without asking, what is verified
and what is assumed.

Built from `specs/SPEC-flipdot.md` with splitflap as the primary template (the
skeleton, the onset detector, the harness and its tools), tinsel for
PassBuffer, the sweep and CI, teletext for the row-scanned update's
discipline (a field index in double), and stencil's copy of repousse's
software-renderer switch.

---

## The one idea

**A disc stays where it is with no power, and the driver reaches it one line
at a time.** Everything about the look falls out of that hardware:

- **The wipe.** A whole-sign change sweeps across at Scan Rate lines a second;
  line L's discs start their swing L / Scan Rate after the pass began.
- **The swing takes time.** Driven from rest by a constant torque, a disc
  arrives at the far stop at its fastest, then rebounds off it; its edge-on
  moment is a thin line, and lit from the side it catches the light.
- **Only what changes moves** — or, with Refresh All, every disc is pulsed and
  those already on their side are driven into their stop and rebound.
- **Bistable.** No update, no change, bit for bit; across a clip trigger too.
- **1-bit**, thresholded or dithered at the pitch.
- **Old signs**: stuck discs, weak coils.

Nothing is animated. The sign is a state machine per disc on the CPU; the
picture is drawn from one angle per disc.

---

## Shape of the code

    source/Controls.*      parameter ids, 0..1 to engineering units, OptionIndex.
    source/Disc.*          the stated angular profile, closed form: the swing
                           and the rebound train.
    source/Sign.*          the sign: every disc's state, the stuck and late
                           sets, the driver's pass. All CPU, all double.
    source/Dither.*        the tone curve, Threshold / Bayer / Floyd-Steinberg.
    source/Onset.*         splitflap's spectral flux over the 64 bins, primed.
    source/Shaders.cpp     copy (mip chain); means (one fragment per disc, its
                           cell's mean luma); board (each disc from its cos and
                           sin, the recess, the rim, the light).
    source/Flipdot.*       the plugin: parameters, the clock, the update
                           decision, the passes, the read-back, test hooks.
    source/PassBuffer.*    tinsel's FFGLFBO with the leak fixed.
    source/{Clock,Diag}    carried from graticule via splitflap.
    tools/fdtest/main.cpp  the harness: seven checks, negatives, --out, --pipe,
                           --bench, --list, --names.
    tools/sweep.py         no control is silently dead, at two rasters.
    tools/verify.sh        all of it, on both renderers. Bash: the pipe step
                           reads PIPESTATUS.
    tools/mutate.sh        one character of the shipped GLSL or the model.
    tools/clips.sh         Resolume's demo clips through the defaults, for a look.

### A frame

1. **copy** the clip into a mipmapped RGBA8 buffer (MaxUV resolved once).
2. **means**, when a pass needs a target (every frame in Continuous, on an
   update otherwise): one R32F fragment per disc, sixteen taps at a whole mip
   level whose texel is at most an eighth of the pitch; luma × alpha.
   `glReadPixels` to the CPU (192 × 108 × 4 bytes at most).
3. **dither** the grid of means to one bit per disc (`Dither.cpp`).
4. **advance the sign** by the frame's delta (`Sign::Advance`): the driver's
   visits this frame, each disc evolved to each visit, pulsed, evolved to now.
5. **upload** cos φ and sin φ of every disc, RG32F, computed in double.
6. **board** to the host's framebuffer.

### The disc (Sign.h)

    side       0 black face out, 1 colour: the stop it is at or heading for
    commanded  what the driver last wrote -- what Only Changes compares with
    queued     a side to swing to the instant it lands, or -1
    motion     rest, swing, kick (a pulse toward its own stop)
    age        seconds since the motion began, double

φ (radians from the black stop) is `side ? θ : π − θ` mid-swing, with
θ = `disc::SwingAngle( age / (Flip Time × weak) )`.

---

## The stated angular profile (Disc.h)

In flip units u = t / Flip Time, θ from the stop the disc left:

- **driven**: θ″ = 2π from rest, so θ = π u², reaching the far stop at u = 1
  exactly, at θ′ = 2π. Flip Time is the time to *first* reach the far stop.
- **rebound**: restitution e (Rebound) at the stop; the latch pulls back at
  4π, so rebound k leaves at 2π e^k, lasts e^k and rises π e^(2k)/2. The
  train is cut once a rebound would rise less than half a degree; from then
  the disc is at the stop exactly (a settled sign is bit-identical).
- **kick** (Refresh All, a pulse toward the face it already shows): the
  rebound train alone, from s = 0. Invisible at e = 0.

---

## Every number in the harness

All seven checks read the rendered picture — never the sign's state —
through the real plugin class, at **640×360 and 320×180**, with a synthetic
60 fps clock and the plugin's clock forced to seconds (one variant forced to
milliseconds from 499,000,000). A disc's side is its centre pixel's red (the
colour face is ≥ 76 under any light, the black face ≤ 20; the cut is 50). A
disc "moved" when its cell's pixels changed (FNV-1a over the cell). The
measurement sign: Square, discs 0.9 of the pitch, yellow, Manual, Only
Changes, no rebound, nothing stuck or late, hard threshold, lit from the
viewer, 4,000 lines a second, 50 ms a swing — unless a check says otherwise.
Every one of them also runs on Apple's software renderer in `verify.sh`.

| Check | The number | Where the tolerance comes from |
| --- | --- | --- |
| `--wipe` | **0 of 144** wrong, 6 runs (column, row, 499 M ms clock × 2 rasters) | A 16×9 sign, black to white, 25 lines/s (2.4 frames a line), a 6.5-frame swing, Rebound 0, lit 80° off-axis. Line L lands 2.4 L + 6.5 frames after the press; its fraction is one of .5 .9 .3 .7 .1, never within **a tenth of a frame** of a whole one, so the landing frame is ⌈2.4 L + 6.5⌉ with nothing to round. The float Scan Rate and Flip Time (a few parts in 10⁷) move it by < 10⁻⁵ frame. "Completed" is the first frame from which the cell is bit-identical to the run's last. The frame before landing is at worst u = 1 − 0.1/6.5, 0.096 rad short of the stop; lit from 80° the face's shade then differs by sin 80° × 0.7 × 0.096 = 0.066, **17 levels**, so it cannot pass for landed. Tolerance: **0 frames**. |
| `--changes` | Only Changes **73 moved = 73 differed**; Refresh All **144 of 144** moved, **71 of 71** unchanged back bit-identical; **0** off target | 144 discs, two seeded pictures. Rebound 0.6 lit from 80°, so a kick rises 0.57 rad: the colour face brightens ~60 levels, the black face's edge moves 16 % of r (1.4 px at r = 9). Exact counts; no tolerance. |
| `--bistable` | **0** of 90, **0** of 60, **0** of 90 frames differed | The default sign on the test card, both rasters: Manual settled (bound: a 0.27 s sweep + a weak disc's 3 × 40 ms swing + its 0.43-flip rebound = 0.4 s; 60 frames given), the clip then inverted with no update, and Continuous sweeping a frozen clip. Bit-identity: no tolerance. |
| `--stuck` | **58** = round(0.1 × 576), **115** = round(0.2 × 576); **0** off the seeded set; **0** of 58 lost at 0.2 | 32×18, three whole-sign changes. The count is exact by construction (the first round(f × N) of a seeded order), so the check asks for it exactly, and that the set seen off the picture is the plugin's. |
| `--rotation` | worst **0.97 / 0.99 px** (r = 72, 640×360), **0.94 / 0.95 px** (r = 36, 320×180); the same on the software renderer | 4×2 (the controls' minimum), one-second swings black→colour and colour→black, Rebound 0.5, 150 frames each; disc (0,0) is on line 0, visited at the press, so frame k is u = k/60 exactly. θ(u) is the harness's **own velocity-Verlet integration** (step 10⁻⁵, impacts solved inside the step) of the torques, not Disc.cpp's closed form. Width = pixels on the centre row at or above half way between the face's own level and 6.1. The shader's coverage is exact on that row, so the count is the pixel centres within ±w: **1 px** for the pixel-centre rule at two edges; + 2 × **0.022 px** for what lies under the edge (rim 9.2 or recess 3.1, against the 6.1 used); + 2 × **0.014 px** for 8-bit rounding of the edge and the centre pixel. Bound 1.07, tolerance **1.1 px**, the same at both rasters because it is about two edges, not r. |
| `--resize` | **0 of 144** changed side across the resize (39 already swung), **0** off target after; **0 of 288** off their parent after 16 → 32 columns | Sides read off centre pixels; exact. |
| `--prime` | fired **0**, moved on **0** of 61 frames; the real onset fired on frame **0** of its step and **144 of 144** turned; after a retrigger fired **0**, changed **0** | All 64 bins at 0.5 from frame 0 (a fire on frame 0 counts: it is the trap). Then 1.0: flux 64(1 − √0.5) = 18.7 against a floor seeded at 64 × 0.707 / 8 = 5.7, × 2.5 = 14.1. Then 0.8 steady across `DeInitGL`/`InitGL`. Every bin gets the same value: nothing assumes the 64 bins are linear, or anything else about them. |
| sweep | **19 of 19** change the picture at both rasters | Two renders per control, low and high of its real range, in a context per control (`tools/sweep.py`). |
| pipe | **27,648** bytes for three 64×36 frames; exit **1** on hang-up | 64 × 36 × 4 × 3. `head -c 1` closes the pipe under a 4.6 MB write. |

## Would this hold on another rasteriser, at another raster?

- **`--wipe`**: bit-identity of a cell against the run's last frame, with a
  17-level margin on the frame before landing and a tenth-of-a-frame margin
  in time. Neither depends on coverage or filtering; the sign's cells are
  whole pixels at both rasters (pitch 40 and 20). Holds anywhere.
- **`--changes`, `--bistable`, `--stuck`, `--resize`**: bit-identity and
  centre-pixel classes 30+ levels either side of the cut. A different
  rasteriser draws different pixels, but draws the *same* pixels for the same
  angle twice, which is all these ask. Holds anywhere GL is deterministic,
  which it must be for the same inputs.
- **`--rotation`**: a fractional edge, and it says so. The count depends only
  on pixel centres (GL puts them at .5 without multisampling) and on the
  shader's own analytic coverage along the centre row, not on the
  rasteriser's. It **did not hold** on the software renderer until the
  shader stopped calling `cos` and `sin` (see the traps); it now matches the
  GPU to the pixel. At 320×180 the 1.1 px is 1.5 % of the disc; the negative
  controls move it 26–102 px.
- **`--prime`**: CPU arithmetic on a synthetic spectrum; no raster involved
  beyond reading sides.
- **The float Scan Rate and Flip Time**: parameters are floats; the harness
  converts with the same `Controls.cpp` functions and the margins above are
  10⁴ times what the rounding can move.
- **Nothing relies on exact cancellation.** A settled disc is at exactly 0 or
  π because the model returns the stop constant once the rebound train is
  cut, not because two floats cancel; its cos and sin are then the same two
  floats every frame (±1, and 0 or sin π = 1.2e-16).
- **The means** are sixteen taps at a whole mip level; a driver's mip
  generation and 8-bit bilinear weights can move a real picture's mean by a
  level or two and so flip a disc on a threshold. The checks feed flat cells
  of 0 or 255, where every tap reads the same value.

## Negative controls

Shipped in `fdtest --negative`, each through a `FlipdotPlugin::Debug` flag the
harness sets on the real plugin; **10 of 10 caught**:

| Broken model | Check | What failed |
| --- | --- | --- |
| the whole sign pulsed at the update (no scan) | `--wipe` | 135 of 144 (by column), 128 (by row) |
| the frame delta taken between two floats of the host clock | `--wipe` | 90 of 144 on the 499 M ms clock; the seconds clock still passes, as it must |
| every disc pulsed under Only Changes | `--changes` | 144 moved against 73 differing, both rasters |
| every disc pulsed under Only Changes | `--bistable` | Continuous: 85 of 90 frames differed |
| stuck discs obeying their coils | `--stuck` | 0 never moved, against 58 and 115 |
| constant angular speed | `--rotation` | off by up to 102 px |
| the swing 15 % slower than Flip Time | `--rotation` | off by up to 73 px |
| the sign cleared on a picture resize (the photofinish bug) | `--resize` | 39 discs changed side, 83 off target |
| the sign cleared on a regrid | `--resize` | 158 of 288 off their parent |
| the onset detector unprimed | `--prime` | a fire on frame 0 and 58 frames of movement; again after the retrigger |

## Mutation test

**By hand, on the committed tree (2026-09-25, 43eefe3), the brief's way.** One
character of the shipped GLSL, in the board shader: `float r = 0.5 * DiscSize
* Pitch;` became `0.6 * DiscSize * Pitch` — every disc drawn 20 % too wide
(clipped at its cell). **`--rotation` failed all four assertions**: worst
26.96 px against r = 72 at 640×360, 13.82 px against r = 36 at 320×180. The
other six checks passed, as they should: they read sides and bit-identity,
and a wider disc is the same wider disc every frame. Reverted with `git
checkout -- source/Shaders.cpp`; `git diff --stat -- source` read empty, and
after the rebuild `--rotation` passed again.

Then `tools/mutate.sh` (re-run on 2026-09-25 after the clock variant joined
`--wipe`), which builds each mutant in its own copy of the tree and never
touches the working tree: **5 of 5 behaved as expected**. The counts are its
`grep -c FAIL`, which includes the check's own summary line: the same radius
mutant (`--rotation`, 4 assertions + 1); `c >= 0.0` → `c <= 0.0` choosing the
face, so a disc shows the side it is not on (`--wipe`, all 6 + 1); the means
shader's cell centre `0.5` → `1.5` in y, each disc's target taken from the
cell below (`--changes`, 4 + 1); `2.0 * kPi` → `3.0 * kPi` in the rebound
depth (`--rotation`, 2 + 1 — only the black→colour swing measures a rebound,
since colour→black rebounds on the black face); and a whitespace-only edit to
`Disc.cpp`, which builds and passes `--rotation`, the control for the
controls.

---

## The traps

Ordered by how much time they cost.

**Apple's software renderer's `cos` is out by 10⁻³ near edge-on.** The first
software-renderer pass of `--rotation` read 1.05 and 1.07 px against a 1.1 px
tolerance whose own derivation came to 1.07, on the same frames that read
0.97 on the GPU. Dumping the centre row showed the edge pixel's coverage
0.45 where 0.525 was due: a disc width 0.075 px narrow at r = 72, i.e.
|cos φ| off by 0.001 near φ = π/2. GLSL 4.10 leaves the precision of the
trigonometric built-ins to the implementation. The board shader now does no
trigonometry: the CPU hands it cos and sin of every disc's angle, and of the
light's, computed in double, and the two renderers agree to the pixel. Without
the `FDTEST_RENDERER=software` pass this would have surfaced as a flaky CI
job on the GPU-less runner.

**A Threshold that is a bias lights a black clip.** With Bayer centred on
Threshold, at 0.3 the matrix's low entries sat below zero and every cell of a
black background lit its dither pattern: the whole bundled Synth clip came out
as an even grid of dots. Threshold is now the luma that maps to one half on a
curve through (0,0), (T,½), (1,1), for every dither, so black stays black and
white stays white whatever it is set to.

**Resolume's bundled clips are dark.** At 3 s the AV clips' mean luma runs
from 0.0 (Bass 001, Bass 005) to 56 of 255 (Beat 001), and fewer than 8 % of
their pixels are above half. At a threshold of 0.5 most of them were a blank
sign with a handful of dots. The default is 0.25.

**A swing's last frame is invisible from the front.** θ = π u² arrives at
the stop at its fastest, but the face's *width* and its front-lit shade both
go as cos, flat at the stop: the frame before landing differs from the landed
one by under a level. A settle detector would call the landing a frame early
and the "exact" wipe would be exact by luck. `--wipe` and `--changes` light
the sign from 80° off-axis, where the shade is linear in the angle near the
stop (17 levels a tenth of a frame out).

**The demo clips' alpha.** The Shop74 clips carry alpha — Trinity_09 is 95 %
transparent at 3 s, OrganicMotions_06 76 % — and the AV clips are opaque.
The sign reads a transparent pixel as black (luma × alpha) and paints its own
face opaque; see the decision below. `tools/clips.sh` writes each still's
alpha range to `alpha.txt`: all ten are 255..255 under the defaults, because
the default 64×36 sign covers a 16:9 frame edge to edge.

**The worktree guard reads the session's directory, not the repo's.** The
session started in a `~/Projects` checkout; `git add` from there, even aimed
at `~/dev/flipdot`, was refused by the hook. `git -C ~/dev/flipdot ...` is
what works (and is the machine's rule anyway).

**What filming found (2026-09-25, the release video).** (1) Switching
Update from Continuous to Manual lets the pass in flight finish, at the scan
rate in force: the video's first cut changed Scan Rate to 30 lines a second
at the switch, so the old pass was still crawling across the right of the
sign when Update Now landed 1.5 s later, and those columns took the new
picture before the new pass reached them. That is the documented "an update
during a pass latches at once" rule, not a defect; the guide now says it, and
the cue sheet changes the rate after the old pass has ended. (2) Lit from 80°
the flat faces fall to ambient plus 0.17 and the sign reads brown; the video
lights its swing beats from 48° and 60°. (3) Onset needs a picture that
changes between passes (Beat 001 barely does); the video's Onset beat is
OrganicMotions_06 under a synthetic spectrum (`fdtest`'s `Spectrum` cue), and
says so. (4) Refresh All's shiver is measurable on film: in the 2x crop, 3x
the frame-to-frame change of Only Changes on the same clip (1.1 % against
0.4 % of pixels a frame).

**zsh's `=word` expansion**, again: `echo =====` as a separator in a one-off
command failed with "===== not found". verify.sh is bash.

**`tools/mutate.sh` built with `-j$(sysctl -n hw.ncpu)`** in splitflap; on a
machine shared with seven other builds that is sixteen compilers at once.
It is `-j4` here.

Inherited from the fleet and honoured without incident: the OBJECT library;
`SetTextParameter` returning success for the About block; the 0..1 clamp on
STANDARD defaults (Columns and Rows are `FF_TYPE_INTEGER`); an option's range
reading back 0..1; `StoatworksAboutParams.h` after the SDK; the viewport
captured before the passes; every `Ensure()` before anything binds a texture;
integer hashing; the reserved GLSL words, `packed` included, and the rest of
4.10's list; the synthetic clock; nothing ever seeing an absolute time; `kPi`
and `<cmath>`, never `M_PI`; no identifier `near` or `far`; `nm | grep -q`
under pipefail avoided by capture.

---

## Decisions taken without asking

**The sign lives on the CPU, not in a float texture.** The spec puts disc
state "in a float texture (side, angle, time since pulse)". The claim the
plugin is built on is timing — line L visited at L / Scan Rate, to the
double, inside a frame — and Resolume's clock cannot be carried in a float,
so the driver and the discs are C++ in double (`Sign.cpp`), at most 20,736
discs. The float texture is the render copy: RG32F, cos and sin of each
disc's angle, uploaded every frame. A picture resize cannot touch the state;
a regrid re-maps it (each new disc takes the old disc at the same place).

**The dither is on the CPU too, after a read-back.** Floyd–Steinberg is
serial. The means pass writes one R32F texel per disc and `glReadPixels`
brings them back — a sync stall once a frame in Continuous, only on an update
otherwise. 3.0 to 3.6 ms a frame at 4K on the largest sign, measured.

**Continuous lags the clip by up to a sweep, by design.** At the default
240 lines a second a column is revisited every 0.27 s, so on Bass 003, which
flashes about twice a second, the sign still shows the last flash in the
columns the driver has not reached again (21 discs lit 10 frames after the
clip went black, 2 — the stuck ones — by 40). That is the driver, not stale
state: a white-then-black test clears to the stuck discs alone.

**The profile is a constant torque to the stop, then damped rebounds.** "Driven
then damped, with a short rebound": driven all the way (θ = π u²), arriving
fastest; the latch pulls back at twice the drive; restitution e; the train cut
below half a degree. Flip Time is the time to first reach the far stop. The
axle is vertical (the face narrows horizontally).

**Refresh All kicks the discs it does not change.** A real pulse toward the
stop a disc already rests on pushes it into the stop, which may move nothing
visible; the spec's check needs "every disc is pulsed" to be seen. The model
treats it as an impact at the arrival speed — the rebound train alone — so it
is honest at e = 0 (invisible) and a shiver otherwise. An open question
below.

**A pulse mid-swing is queued, not a reversal.** The coil cannot turn a disc
round in the air; the command waits and fires the instant the disc lands. A
pulse during a rebound starts the new swing from the stop (a jump of at most
the rebound's depth, 32° at e = 0.6).

**Updates.** Continuous: the driver sweeps for ever, wrapping to line 0,
against the live clip. Interval, Onset, Manual: an update latches the picture
and starts one pass; an update during a pass latches the new picture at once
(the lines not yet visited get it) and queues one more pass on the same
cadence. Interval ticks from the mode's start; a stall gives one update, not
a burst.

**The first frame fires in Continuous and Interval only.** The spec's
`--prime` wants no update on the first frame in Onset mode; Manual likewise
waits for a press. The sign then shows what it held: blank on a new instance.

**Bistable across a clip trigger.** `InitGL` does not reset the sign, only a
new instance starts blank. `--prime` checks it.

**Stuck is exact, seeded, nested.** The stuck set is the first round(f × N)
discs of a seeded order of all N, so the count is the stated fraction to the
disc and raising Stuck only adds discs. Each stuck disc's side is seeded
(roughly half on the colour face). A disc freed by lowering Stuck stays where
it was jammed, and the driver's memory is not corrected (it never knew), so
under Only Changes it is fixed only when its target next changes.

**Late is a weak coil**: 1.5 to 3 times the swing time, seeded, on
round(f × N) discs.

**Output alpha: the sign is opaque.** Inside the sign's rectangle the output
is `mix( source, board, Mix )` with the board's alpha 1, so at Mix 1 the
output is opaque over a transparent clip — a physical sign has no holes — and
a transparent pixel of the clip reads as black to the discs (luma × alpha).
The surround a non-matching aspect leaves is transparent (board alpha 0), as
splitflap's letterbox is, so a small sign composites over the layers below.

**Square pitch, as large as fits, centred.** Discs are round, so the pitch is
square; the default 64×36 fits 16:9 exactly.

**The light is in the horizontal plane**, from the viewer (Light 0) to 80°
from the left (Light 1), Lambert with ambient 0.3; the recess shades the far
side of each disc from an oblique light, up to 45 % at the edge — the "lit
side". The black face is 0.08, the sign face 0.035, the recess 0.012, the rim
0.12.

**Threshold is a tone curve**, not a bias (see the traps).

**Defaults**, from the demo clips: 64×36, yellow, 240 lines a second (a
0.27 s sweep), Continuous, Only Changes, 40 ms a swing (2.4 frames: a frame
in two catches a disc mid-swing), Rebound 0.3, Stuck 0.3 % (7 dead dots),
Late 2 %, Bayer at 0.25, Light 0.3 (24°).

**Onset is splitflap's detector unchanged**: one detector over all 64 bins,
root magnitude, flux against a one-second floor seeded at an eighth of the
level, 2.5 × and 0.02, 100 ms refractory. No bin is mapped to a frequency.

**The frame delta is clamped to 0.25 s**, so a scrub advances the sign by a
quarter second, not a whole pass.

---

## The browser demo (`demo/`)

<https://flipdot-demo.stoatworks-labs.com/>, built 2026-09-25 from the fleet
kit (`stoatworks-backend/resolume-demo`). **Two halves, not equally
faithful:**

- **The shaders are the plugin's.** `demo/plugin.js` carries the four
  `Shaders.cpp` constants, written by `demo/tools/splice_shaders.py`;
  `demo/tools/check_shaders.py` (in `verify.sh`) fails on one character's
  drift. WebGL2 runs copy → means (R32F, read back) → board, as the plugin
  does.
- **The CPU half is a PORT**, `demo/sign.js`: Sign, Disc, Dither, Controls,
  Onset, and ProcessOpenGL's sequence with decideUpdate and uploadAngles,
  `Math.fround` wherever the C++ holds a float. `demo/tools/check_port.sh`
  (in `verify.sh`) compiles refsign.cpp against Sign/Disc/Dither/Controls/
  Onset/Clock.cpp unchanged plus text **cut** from Flipdot.h/.cpp at run time
  (the constructor, decideUpdate, uploadAngles, ProcessOpenGL,
  SetFloatParameter, SetTime), with GL stood in for by
  `demo/tools/stub/FFGLSDK.h`. Measured 2026-09-25: all 20 declarations
  (name, type, group, default, range, elements) identical to the page's; 22
  laws at 1,012 host values, the profile at 4,001 points for nine
  restitutions, geometryFor at 1,440 sizes identical; **799 frames** over
  eight cases (defaults, the dither edges, Continuous→Manual with Refresh
  All, Interval with stalls and a backwards clock, regrids with Stuck and
  Late at their maximum, 192×108 to 4×2 and a picture unlike the viewport,
  the range ends, Onset on silence) identical in every angle, target bit,
  cos/sin float and uniform. Built a second time with the Release flags
  (`-O3`, clang's default FP contraction, arm64): **0** frames differ. Its
  own mutants: the rebound cut moved by a hair, the frame clamp at 0.26,
  Interval's first-frame fire removed, the pending pass dropped — all caught;
  the swing's 1e-6 s floor raised to 1e-3 s survives, and is equivalent (Flip
  Time's minimum is 5 ms).
- **What it cannot see:** the means themselves (the script's, not a GPU's),
  the board pass, whether plugin.js's GL calls match the plugin's, and the
  browser's own `Math.pow`/`cos`/`sin` (it ran under Node).
- **Gaps the page states:** no audio (Onset never fires; nothing fakes one);
  Update Now is a button; Columns/Rows are dropdowns; the read-back is
  RGBA/FLOAT (WebGL2's guarantee) keeping red; the browser makes its own mip
  chain; the clock is declared seconds, 0.25 s clamp ported; no retrigger;
  MaxUV (1, 1); no alpha clip (straight vs premultiplied is open); **no
  performance claim** (the read-back stall inside Resolume is unmeasured); no
  About block.
- **Deploy:** a Worker route on a proxied `AAAA 100::` record made through the
  API (the zone's 100 Workers custom domains are used up); a push to `main`
  deploys (`deploy.yml`, repo secret `CLOUDFLARE_API_TOKEN`, variable
  `CLOUDFLARE_ACCOUNT_ID`) and checks the live `<head>`.

## What is genuinely verified, and what is assumed

**Verified, by measurement on this machine (M4 Max, macOS 26.4.1),
`tools/verify.sh` green:** everything in the table above, at both rasters, on
the GPU and on Apple's software renderer; all ten negative controls caught;
the mutation test, by hand and scripted; 19 of 19 controls alive at both
rasters; the pipe's byte count and hang-up status; the four shaders through
glslc; the bundle universal, exporting `plugMain`, ad-hoc signed, probed under
oxbow as SW Flipdot / FD01 / effect and rendering frames there; the render
cost (README).

**Looked at, not measured:** ten of Resolume's bundled demo clips through the
defaults, and through Bayer and Floyd–Steinberg at 0.25 and 0.3
(`tools/clips.sh`): the bright clips read as their picture in dots, the dark
ones as a sparse sign with the stuck dots showing, and nothing floods.

**Assumed, or not done:**

- **Never loaded into Resolume on macOS.** On Windows it has been: the
  fleet's Arena gate (plugin-bench `arena/expect/flipdot.json`) passed 9 of
  9 on Resolume Arena 7.27.1, software rendering, 2026-09-25: 26 controls
  match, 12 live, the seven that act only while discs move (Scan, Scan Rate,
  Update, Interval, Flip Time, Rebound, Late) annotated `inert` because the
  gate's still carrier settles the sign once and a settled sign is the same
  picture at any timing, Audio skipped (no sound device).
- **No real audio.** The detector's constants are splitflap's, from
  synthetic spectra; the bins' law is unmeasured and nothing assumes one.
- **Whether Resolume hands over straight or premultiplied alpha.** luma ×
  alpha is right for straight; for premultiplied it darkens a soft edge twice.
- **The read-back's stall inside Resolume** is unmeasured (a synchronous
  `glReadPixels` once a frame in Continuous).
- No presets, no OpenFX port.

## Open questions

1. **Refresh All's kick.** A real disc driven into its own stop may not move
   visibly at all. Is the shiver wanted, or should Refresh All be visible only
   in timing?
2. **Mid-swing commands** are queued; a stronger model would reverse the disc
   in the air with the new torque.
3. **The read-back** is a sync stall a frame. A PBO read one frame late would
   hide it, at a frame of latency on the target.
4. **Horizontal axles.** Many signs pivot their discs about a horizontal
   diameter; an option would be cheap.
5. **Stuck discs half-turned.** Real dead dots are sometimes jammed edge-on;
   these are always at a stop.
6. **Floyd–Steinberg in Continuous** re-diffuses the whole picture every frame,
   so noise in a clip becomes discs flipping all over the sign. Authentic, but
   a temporal hysteresis on the target might be kinder.
7. **The onset constants** want programme material through Resolume's FFT.
