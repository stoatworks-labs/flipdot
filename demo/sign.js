/**
 * Flipdot's CPU half, ported to JavaScript: a PORT, not the plugin's code.
 *
 * The plugin keeps the sign on the CPU in double (source/Sign.cpp, with the
 * disc's stated profile in source/Disc.cpp), dithers on the CPU after a
 * read-back of every disc's mean luma (source/Dither.cpp), converts every
 * control in source/Controls.cpp, and sequences a frame in
 * FlipdotPlugin::ProcessOpenGL and decideUpdate (source/Flipdot.cpp). This
 * file is all of that, function for function, in JavaScript numbers -- IEEE
 * doubles, as the C++'s are -- with Math.fround wherever the C++ holds or
 * computes a float: the host's parameters, the angles, the uploaded cos and
 * sin, the geometry, the disc size and the light.
 *
 * It has no DOM and no GL, so Node can import it: demo/tools/check_port.mjs
 * builds the plugin's own Sign.cpp, Disc.cpp, Dither.cpp, Controls.cpp,
 * Onset.cpp and Clock.cpp, and text cut from Flipdot.h and Flipdot.cpp at run
 * time (the constructor, decideUpdate, uploadAngles, ProcessOpenGL,
 * SetFloatParameter, SetTime), against stand-ins for GL, and compares this
 * file with it frame by frame. See that script for exactly what it covers and
 * what it cannot. The page (plugin.js) runs the GPU passes; this runs the rest.
 *
 * Not ported, and absent: the Clock's unit vote (the page declares seconds,
 * as the harness does), the negative controls (Debug flags; always off in the
 * shipped plugin), diag logging, and the About block.
 */

//===========================================================================
// Controls.h / Controls.cpp
//===========================================================================

export const PT = {
  COLUMNS: 0, ROWS: 1, LAYOUT: 2, DISC_SIZE: 3, COLOUR: 4,
  SCAN: 5, SCAN_RATE: 6, UPDATE: 7, INTERVAL: 8, ONLY_CHANGES: 9, UPDATE_NOW: 10, AUDIO: 11,
  FLIP_TIME: 12, REBOUND: 13, STUCK: 14, LATE: 15,
  DITHER: 16, THRESHOLD: 17, LIGHT: 18, MIX: 19,
  ABOUT_TEXT: 20,
};
export const K_AUDIO_BINS = 64;

export const K_COLUMNS_MIN = 4, K_COLUMNS_MAX = 192, K_COLUMNS_DEFAULT = 64;
export const K_ROWS_MIN = 2, K_ROWS_MAX = 108, K_ROWS_DEFAULT = 36;

export const K_LAYOUT_SQUARE = 0, K_LAYOUT_OFFSET = 1, K_LAYOUT_COUNT = 2;
export const K_COLOUR_YELLOW = 0, K_COLOUR_GREEN = 1, K_COLOUR_WHITE = 2, K_COLOUR_COUNT = 3;
export const K_SCAN_COLUMNS = 0, K_SCAN_ROWS = 1, K_SCAN_COUNT = 2;
export const K_UPDATE_CONTINUOUS = 0, K_UPDATE_INTERVAL = 1, K_UPDATE_ONSET = 2, K_UPDATE_MANUAL = 3, K_UPDATE_COUNT = 4;
export const K_DITHER_THRESHOLD = 0, K_DITHER_BAYER = 1, K_DITHER_FLOYD = 2, K_DITHER_COUNT = 3;

const f32 = Math.fround;
const clamp01 = (v) => Math.min(Math.max(v, 0.0), 1.0);
const lerp = (from, to, t) => from + (to - from) * clamp01(t);
const lerpInverse = (from, to, x) => clamp01((x - from) / (to - from));
const geometric = (from, to, t) => from * Math.pow(to / from, clamp01(t));
const geometricInverse = (from, to, x) => clamp01(Math.log(x / from) / Math.log(to / from));

const K_SCAN_MIN = 4.0, K_SCAN_MAX = 4000.0;
const K_INTERVAL_MIN = 0.1, K_INTERVAL_MAX = 10.0;
const K_FLIP_MIN = 0.005, K_FLIP_MAX = 1.0;
const K_REBOUND_MAX = 0.6;
const K_STUCK_MAX = 0.2;
const K_LATE_MAX = 0.5;

/** OptionIndex. `value` is a float as the host stores it. */
export function optionIndex(value, count) {
  if (count <= 1) return 0;
  let v = f32(value);
  // std::round on a float, std::fabs, and the float literal 1e-4f.
  if (v > 0 && v <= 1 && Math.abs(f32(v - roundHalfAway(v))) > f32(1e-4)) v = f32(v * f32(count - 1));
  const index = roundHalfAway(v);
  return Math.min(Math.max(index, 0), count - 1);
}

/** std::lround / std::round: half away from zero (Math.round is half up). */
function roundHalfAway(v) {
  return v < 0 ? -Math.round(-v) : Math.round(v);
}

export const discSizeFromParam = (v) => f32(lerp(0.4, 1.0, f32(v)));
export const scanRateFromParam = (v) => geometric(K_SCAN_MIN, K_SCAN_MAX, f32(v));
export const scanRateToParam = (x) => f32(geometricInverse(K_SCAN_MIN, K_SCAN_MAX, x));
export const intervalFromParam = (v) => geometric(K_INTERVAL_MIN, K_INTERVAL_MAX, f32(v));
export const intervalToParam = (x) => f32(geometricInverse(K_INTERVAL_MIN, K_INTERVAL_MAX, x));
export const flipTimeFromParam = (v) => geometric(K_FLIP_MIN, K_FLIP_MAX, f32(v));
export const flipTimeToParam = (x) => f32(geometricInverse(K_FLIP_MIN, K_FLIP_MAX, x));
export const reboundFromParam = (v) => lerp(0.0, K_REBOUND_MAX, f32(v));
export const reboundToParam = (x) => f32(lerpInverse(0.0, K_REBOUND_MAX, x));
export const stuckFromParam = (v) => lerp(0.0, K_STUCK_MAX, f32(v));
export const stuckToParam = (x) => f32(lerpInverse(0.0, K_STUCK_MAX, x));
export const lateFromParam = (v) => lerp(0.0, K_LATE_MAX, f32(v));
export const lateToParam = (x) => f32(lerpInverse(0.0, K_LATE_MAX, x));
const K_EIGHTY_DEGREES = 1.3962634015954636;
export const lightAngleFromParam = (v) => f32(lerp(0.0, K_EIGHTY_DEGREES, f32(v)));

//===========================================================================
// Disc.h / Disc.cpp: the stated angular profile.
//===========================================================================

export const K_PI = 3.14159265358979323846;
const K_REST_ANGLE = K_PI / 360.0;
const K_MAX_REBOUNDS = 40;
const reboundHappens = (ek) => K_PI * ek * ek * 0.5 >= K_REST_ANGLE;

export function reboundDepth(s, restitution) {
  if (!(restitution > 0.0) || s < 0.0) return 0.0;
  let ek = 1.0;
  for (let k = 1; k <= K_MAX_REBOUNDS; k += 1) {
    ek *= restitution;
    if (!reboundHappens(ek)) return 0.0;
    if (s < ek) return 2.0 * K_PI * s * (ek - s);
    s -= ek;
  }
  return 0.0;
}

export function reboundEnd(restitution) {
  if (!(restitution > 0.0)) return 0.0;
  let end = 0.0, ek = 1.0;
  for (let k = 1; k <= K_MAX_REBOUNDS; k += 1) {
    ek *= restitution;
    if (!reboundHappens(ek)) break;
    end += ek;
  }
  return end;
}

export function swingAngle(u, restitution) {
  if (u <= 0.0) return 0.0;
  if (u < 1.0) return K_PI * u * u;
  return K_PI - reboundDepth(u - 1.0, restitution);
}

//===========================================================================
// Dither.h / Dither.cpp. Row 0 is the top of the sign.
//===========================================================================

const K_BAYER = [
  [0, 8, 2, 10],
  [12, 4, 14, 6],
  [3, 11, 1, 9],
  [15, 7, 13, 5],
];

export function toneCurve(luma, threshold) {
  const t = Math.min(Math.max(threshold, 0.001), 0.999);
  const v = Math.min(Math.max(luma, 0.0), 1.0);
  return v < t ? 0.5 * v / t : 0.5 + 0.5 * (v - t) / (1.0 - t);
}

/** DitherGrid. `luma` is a Float32Array, `threshold` a float; returns a Uint8Array. */
export function ditherGrid(luma, columns, rows, mode, threshold) {
  const n = columns * rows;
  const bits = new Uint8Array(n);
  if (luma.length < n) return bits;
  const tth = f32(threshold);
  switch (mode) {
    case K_DITHER_BAYER:
      for (let r = 0; r < rows; r += 1) {
        for (let c = 0; c < columns; c += 1) {
          const level = (K_BAYER[r & 3][c & 3] + 0.5) / 16.0;
          const i = r * columns + c;
          bits[i] = toneCurve(luma[i], tth) >= level ? 1 : 0;
        }
      }
      break;
    case K_DITHER_FLOYD: {
      let here = new Float64Array(columns + 2);
      let below = new Float64Array(columns + 2);
      for (let r = 0; r < rows; r += 1) {
        const rightward = (r & 1) === 0;
        below.fill(0.0);
        for (let k = 0; k < columns; k += 1) {
          const c = rightward ? k : columns - 1 - k;
          const dir = rightward ? 1 : -1;
          const i = r * columns + c;
          const v = toneCurve(luma[i], tth) + here[c + 1];
          const bit = v >= 0.5 ? 1 : 0;
          bits[i] = bit;
          const err = v - bit;
          here[c + 1 + dir] += err * 7.0 / 16.0;
          below[c + 1 - dir] += err * 3.0 / 16.0;
          below[c + 1] += err * 5.0 / 16.0;
          below[c + 1 + dir] += err * 1.0 / 16.0;
        }
        const swap = here;
        here = below;
        below = swap;
      }
      break;
    }
    case K_DITHER_THRESHOLD:
    default:
      for (let i = 0; i < n; i += 1) bits[i] = toneCurve(luma[i], tth) >= 0.5 ? 1 : 0;
      break;
  }
  return bits;
}

//===========================================================================
// Sign.h / Sign.cpp: every disc's state and the driver, in double.
//===========================================================================

/** The PCG output permutation, exact in 32-bit integers (uint32_t in the C++). */
export function hashInt(x) {
  x = (Math.imul(x >>> 0, 747796405) + 2891336453) >>> 0;
  x = Math.imul(((x >>> ((x >>> 28) + 4)) ^ x) >>> 0, 277803737) >>> 0;
  return ((x >>> 22) ^ x) >>> 0;
}

export function discHash(column, row, salt) {
  return hashInt((hashInt((Math.imul(column >>> 0, 2654435761) ^ salt) >>> 0) + Math.imul(row >>> 0, 40503)) >>> 0);
}

const hash01 = (h) => (h & 0x00ffffff) / 16777216.0;

const K_STUCK_SALT = 0x57, K_SIDE_SALT = 0x5d, K_LATE_SALT = 0x1a, K_WEAK_SALT = 0xe7;

export const K_REST = 0, K_SWING = 1, K_KICK = 2;

export class Sign {
  constructor() {
    this.columns = 0;
    this.rows = 0;
    this.stuck = -1.0;
    this.late = -1.0;
    this.stuckCount = 0;
    this.n = 0;
    // Disc, as parallel arrays.
    this.side = new Uint8Array(0);
    this.commanded = new Uint8Array(0);
    this.queued = new Int8Array(0);
    this.motion = new Uint8Array(0);
    this.age = new Float64Array(0);
    // Traits.
    this.tStuck = new Uint8Array(0);
    this.tStuckSide = new Uint8Array(0);
    this.tWeak = new Float64Array(0);
    this.traitsSize = 0;
    this.angles = new Float32Array(0);
    // The driver.
    this.active = false;
    this.pending = false;
    this.wasContinuous = false;
    this.head = 0.0;
    this.next = 0;
    this.latched = new Uint8Array(0);
    this.latchedSize = 0;
    this.visits = [];
  }

  reset() {
    this.side.fill(0);
    this.commanded.fill(0);
    this.queued.fill(-1);
    this.motion.fill(K_REST);
    this.age.fill(0.0);
    this.active = false;
    this.pending = false;
    this.head = 0.0;
    this.next = 0;
    this.latched.fill(0);
  }

  configure(columns, rows, stuck, late) {
    columns = Math.max(columns, 1);
    rows = Math.max(rows, 1);
    if (columns !== this.columns || rows !== this.rows) {
      const n = columns * rows;
      const side = new Uint8Array(n);
      const commanded = new Uint8Array(n);
      const queued = new Int8Array(n).fill(-1);
      const motion = new Uint8Array(n);
      const age = new Float64Array(n);
      const latched = new Uint8Array(n);
      if (this.columns > 0 && this.rows > 0 && this.n > 0) {
        for (let r = 0; r < rows; r += 1) {
          for (let c = 0; c < columns; c += 1) {
            const oc = Math.min(Math.trunc(c * this.columns / columns), this.columns - 1);
            const orow = Math.min(Math.trunc(r * this.rows / rows), this.rows - 1);
            const i = r * columns + c;
            const o = orow * this.columns + oc;
            side[i] = this.side[o];
            commanded[i] = this.commanded[o];
            queued[i] = this.queued[o];
            motion[i] = this.motion[o];
            age[i] = this.age[o];
            if (this.latchedSize > 0) latched[i] = this.latched[o];
          }
        }
      }
      this.columns = columns;
      this.rows = rows;
      this.n = n;
      this.side = side;
      this.commanded = commanded;
      this.queued = queued;
      this.motion = motion;
      this.age = age;
      this.latched = latched;
      this.latchedSize = n;
      this.angles = new Float32Array(n);
      this.stuck = -1.0; // reseed
    }
    if (stuck !== this.stuck || late !== this.late) this.seed(stuck, late);
  }

  seed(stuck, late) {
    const n = this.n;
    const cols = this.columns;
    const tStuck = new Uint8Array(n);
    const tStuckSide = new Uint8Array(n);
    const tWeak = new Float64Array(n).fill(1.0);

    const firstOf = (salt, fraction) => {
      const key = new Uint32Array(n);
      for (let i = 0; i < n; i += 1) key[i] = discHash(i % cols, Math.trunc(i / cols), salt);
      const order = new Array(n);
      for (let i = 0; i < n; i += 1) order[i] = i;
      order.sort((a, b) => (key[a] !== key[b] ? (key[a] < key[b] ? -1 : 1) : a - b));
      const count = Math.round(Math.min(Math.max(fraction, 0.0), 1.0) * n);
      return order.slice(0, Math.min(count, n));
    };

    for (const i of firstOf(K_STUCK_SALT, stuck)) {
      tStuck[i] = 1;
      tStuckSide[i] = discHash(i % cols, Math.trunc(i / cols), K_SIDE_SALT) >>> 31;
    }
    for (const i of firstOf(K_LATE_SALT, late)) {
      tWeak[i] = 1.5 + 1.5 * hash01(discHash(i % cols, Math.trunc(i / cols), K_WEAK_SALT));
    }

    // A disc freed from being stuck is where it was jammed, at rest. The
    // driver's memory is left alone: it never knew.
    if (n === this.traitsSize) {
      for (let i = 0; i < n; i += 1) {
        if (this.tStuck[i] && !tStuck[i]) {
          this.side[i] = this.tStuckSide[i];
          this.motion[i] = K_REST;
          this.queued[i] = -1;
          this.age[i] = 0.0;
        }
      }
    }

    let count = 0;
    for (let i = 0; i < n; i += 1) count += tStuck[i];
    this.stuckCount = count;
    this.tStuck = tStuck;
    this.tStuckSide = tStuckSide;
    this.tWeak = tWeak;
    this.traitsSize = n;
    this.stuck = stuck;
    this.late = late;
  }

  swingSeconds(i, s) {
    return Math.max(s.flipTime * this.tWeak[i] * 1.0, 1.0e-6);
  }

  evolve(i, dt, s) {
    if (this.motion[i] === K_REST) return;
    this.age[i] += Math.max(dt, 0.0);
    const T = this.swingSeconds(i, s);
    const end = reboundEnd(s.restitution);
    for (let guard = 0; guard < 16; guard += 1) {
      if (this.motion[i] === K_SWING) {
        if (this.age[i] >= T && this.queued[i] >= 0) {
          if (this.queued[i] !== this.side[i]) {
            this.age[i] -= T;
            this.side[i] = this.queued[i];
            this.queued[i] = -1;
            continue;
          }
          this.queued[i] = -1;
        }
        if (this.age[i] >= T * (1.0 + end)) {
          this.motion[i] = K_REST;
          this.age[i] = 0.0;
        }
      } else if (this.motion[i] === K_KICK) {
        if (this.age[i] >= T * end) {
          this.motion[i] = K_REST;
          this.age[i] = 0.0;
        }
      }
      break;
    }
  }

  pulse(i, desired, s) {
    if (s.onlyChanges && desired === this.commanded[i]) return;
    this.commanded[i] = desired;
    if (this.tStuck[i]) return;
    const T = this.swingSeconds(i, s);
    if (this.motion[i] === K_SWING && this.age[i] < T) {
      this.queued[i] = desired !== this.side[i] ? desired : -1;
      return;
    }
    if (desired !== this.side[i]) {
      this.side[i] = desired;
      this.motion[i] = K_SWING;
      this.age[i] = 0.0;
      this.queued[i] = -1;
      return;
    }
    if (s.restitution > 0.0) {
      this.motion[i] = K_KICK;
      this.age[i] = 0.0;
      this.queued[i] = -1;
    }
  }

  angleOf(i, s) {
    if (this.tStuck[i]) return this.tStuckSide[i] ? f32(K_PI) : 0.0;
    const T = this.swingSeconds(i, s);
    let phi = this.side[i] ? K_PI : 0.0;
    if (this.motion[i] === K_SWING) {
      const theta = swingAngle(this.age[i] / T, s.restitution);
      phi = this.side[i] ? theta : K_PI - theta;
    } else if (this.motion[i] === K_KICK) {
      const depth = reboundDepth(this.age[i] / T, s.restitution);
      phi = this.side[i] ? K_PI - depth : depth;
    }
    return f32(phi);
  }

  /** Sign::Advance. `target` is a Uint8Array, row 0 the top, or null. */
  advance(dt, fire, target, s) {
    const lines = s.scanRows ? this.rows : this.columns;
    const n = this.n;
    if (lines <= 0 || n === 0) return;
    dt = Math.max(dt, 0.0);
    const rate = Math.max(s.scanRate, 1.0e-6);

    if (this.next > lines) {
      this.next = lines;
      this.head = Math.min(this.head, lines);
    }
    const wasActive = this.active;
    if (s.continuous || fire) {
      if (target && target.length === n) this.latched = Uint8Array.from(target);
      if (this.active) this.pending = true;
      else {
        this.active = true;
        this.head = 0.0;
        this.next = 0;
      }
    }
    if (!s.continuous && this.wasContinuous) this.pending = false;
    this.wasContinuous = s.continuous;
    if (wasActive) this.head += dt * rate;

    this.visits.length = lines;
    for (let l = 0; l < lines; l += 1) {
      if (this.visits[l]) this.visits[l].length = 0;
      else this.visits[l] = [];
    }

    const limit = 64 * lines + 64;
    for (let guard = 0; this.active && guard < limit; guard += 1) {
      if (this.next >= lines) {
        if (this.pending || s.continuous) {
          this.pending = s.continuous;
          this.head -= this.next;
          this.next = 0;
        } else {
          this.active = false;
          this.head = 0.0;
          this.next = 0;
        }
        continue;
      }
      if (this.next > this.head) break;
      this.visits[this.next].push((this.head - this.next) / rate);
      this.next += 1;
    }

    for (let r = 0; r < this.rows; r += 1) {
      for (let c = 0; c < this.columns; c += 1) {
        const i = r * this.columns + c;
        let at = -dt;
        const list = this.visits[s.scanRows ? r : c];
        for (let v = 0; v < list.length; v += 1) {
          const when = -list[v];
          this.evolve(i, when - at, s);
          at = Math.max(at, when);
          this.pulse(i, this.latched[i], s);
        }
        this.evolve(i, -at, s);
        this.angles[i] = this.angleOf(i, s);
      }
    }
  }
}

//===========================================================================
// Onset.h / Onset.cpp. On the page it is handed silence every frame -- no
// spectrum reaches a browser -- which it reads exactly as the plugin reads an
// unrouted input.
//===========================================================================

const K_FLOOR_SECONDS = 1.0, K_RATIO = 2.5, K_MIN_FLUX = 0.02, K_REFRACTORY = 0.10, K_PRIME_FRACTION = 0.125;

export class Onset {
  constructor() {
    this.reset();
  }

  reset() {
    this.prev = new Float64Array(K_AUDIO_BINS);
    this.floor = 0.0;
    this.flux = 0.0;
    this.fluxPrev = 0.0;
    this.lastFire = -1.0e9;
    this.lastSeconds = 0.0;
    this.primed = false;
  }

  frame(seconds, bins, count) {
    const m = new Float64Array(K_AUDIO_BINS);
    let level = 0.0;
    for (let i = 0; i < K_AUDIO_BINS; i += 1) {
      const v = bins !== null && i < count ? f32(bins[i]) : 0.0;
      const clean = v > 0.0 ? v : 0.0;
      m[i] = Math.sqrt(clean);
      level += m[i];
    }
    if (!this.primed) {
      this.prev = m;
      this.floor = level * K_PRIME_FRACTION;
      this.fluxPrev = 0.0;
      this.lastSeconds = seconds;
      this.lastFire = seconds - K_REFRACTORY;
      this.primed = true;
      return false;
    }
    const dt = Math.max(seconds - this.lastSeconds, 0.0);
    this.lastSeconds = seconds;
    let flux = 0.0;
    for (let i = 0; i < K_AUDIO_BINS; i += 1) flux += Math.max(m[i] - this.prev[i], 0.0);
    this.prev = m;
    let fired = false;
    if (flux > Math.max(K_RATIO * this.floor, K_MIN_FLUX) && flux > this.fluxPrev && seconds - this.lastFire >= K_REFRACTORY) {
      fired = true;
      this.lastFire = seconds;
    }
    if (dt > 0.0) {
      const a = 1.0 - Math.exp(-dt / K_FLOOR_SECONDS);
      this.floor += (flux - this.floor) * a;
    }
    this.fluxPrev = flux;
    this.flux = flux;
    return fired;
  }
}

//===========================================================================
// Flipdot.cpp: the constructor's declarations and defaults, decideUpdate,
// uploadAngles and ProcessOpenGL, minus the GL.
//===========================================================================

export const LAYOUT_NAMES = ['Square', 'Offset'];
export const COLOUR_NAMES = ['Yellow', 'Green', 'White'];
export const SCAN_NAMES = ['Column by Column', 'Row by Row'];
export const UPDATE_NAMES = ['Continuous', 'Interval', 'Onset', 'Manual'];
export const DITHER_NAMES = ['Threshold', 'Bayer 4x4', 'Floyd-Steinberg'];

/** The fluorescent face of each Colour, as the float literals the C++ holds. */
export const FACES = [
  [f32(1.00), f32(0.84), f32(0.06)],
  [f32(0.55), f32(1.00), f32(0.18)],
  [f32(0.94), f32(0.94), f32(0.90)],
];

const K_MAX_FRAME_DELTA = 0.25;

/**
 * FlipdotPlugin::FlipdotPlugin(), as declared: every parameter the plugin
 * declares, in order, with its name, FFGL type, host-side default, range,
 * elements and group. The page's inspector is built from this list, and
 * check_port.mjs compares it with what the constructor itself declares.
 * The Audio buffer and the About block are declared by the plugin and are
 * listed here so that comparison is whole; the page draws neither.
 */
export const DECLARED = [
  { index: PT.COLUMNS, id: 'columns', name: 'Columns', type: 'integer', default: K_COLUMNS_DEFAULT, range: [K_COLUMNS_MIN, K_COLUMNS_MAX], group: 'Sign' },
  { index: PT.ROWS, id: 'rows', name: 'Rows', type: 'integer', default: K_ROWS_DEFAULT, range: [K_ROWS_MIN, K_ROWS_MAX], group: 'Sign' },
  { index: PT.LAYOUT, id: 'layout', name: 'Layout', type: 'option', default: K_LAYOUT_SQUARE, elements: LAYOUT_NAMES, group: 'Sign' },
  { index: PT.DISC_SIZE, id: 'discSize', name: 'Disc Size', type: 'standard', default: f32(0.85), group: 'Sign' },
  { index: PT.COLOUR, id: 'colour', name: 'Colour', type: 'option', default: K_COLOUR_YELLOW, elements: COLOUR_NAMES, group: 'Sign' },
  { index: PT.SCAN, id: 'scan', name: 'Scan', type: 'option', default: K_SCAN_COLUMNS, elements: SCAN_NAMES, group: 'Driver' },
  { index: PT.SCAN_RATE, id: 'scanRate', name: 'Scan Rate', type: 'standard', default: scanRateToParam(240.0), group: 'Driver' },
  { index: PT.UPDATE, id: 'update', name: 'Update', type: 'option', default: K_UPDATE_CONTINUOUS, elements: UPDATE_NAMES, group: 'Driver' },
  { index: PT.INTERVAL, id: 'interval', name: 'Interval', type: 'standard', default: intervalToParam(2.0), group: 'Driver' },
  { index: PT.ONLY_CHANGES, id: 'onlyChanges', name: 'Only Changes', type: 'boolean', default: 1, group: 'Driver' },
  { index: PT.UPDATE_NOW, id: 'updateNow', name: 'Update Now', type: 'event', default: 0, group: 'Driver' },
  { index: PT.AUDIO, id: 'audio', name: 'Audio', type: 'buffer', default: 0, bins: K_AUDIO_BINS, group: 'Driver', page: false },
  { index: PT.FLIP_TIME, id: 'flipTime', name: 'Flip Time', type: 'standard', default: flipTimeToParam(0.04), group: 'Discs' },
  { index: PT.REBOUND, id: 'rebound', name: 'Rebound', type: 'standard', default: reboundToParam(0.3), group: 'Discs' },
  { index: PT.STUCK, id: 'stuck', name: 'Stuck', type: 'standard', default: stuckToParam(0.003), group: 'Discs' },
  { index: PT.LATE, id: 'late', name: 'Late', type: 'standard', default: lateToParam(0.02), group: 'Discs' },
  { index: PT.DITHER, id: 'dither', name: 'Dither', type: 'option', default: K_DITHER_BAYER, elements: DITHER_NAMES, group: 'Look' },
  { index: PT.THRESHOLD, id: 'threshold', name: 'Threshold', type: 'standard', default: f32(0.25), group: 'Look' },
  { index: PT.LIGHT, id: 'light', name: 'Light', type: 'standard', default: f32(0.3), group: 'Look' },
  { index: PT.MIX, id: 'mix', name: 'Mix', type: 'standard', default: f32(1.0), group: 'Look' },
];

export const intParam = (value, lo, hi) => Math.min(Math.max(roundHalfAway(f32(value)), lo), hi);

/** geometryFor, in float as the C++ is. */
export function geometryFor(pictureWidth, pictureHeight, columns, rows, offset) {
  const across = f32(f32(columns) + (offset ? 0.5 : 0.0));
  const pitch = Math.min(f32(pictureWidth / across), f32(pictureHeight / rows));
  const width = f32(pitch * across);
  const height = f32(pitch * rows);
  const originX = f32(0.5 * f32(pictureWidth - width));
  const originY = f32(0.5 * f32(pictureHeight - height));
  return { pitch, originX, originY, width, height };
}

/**
 * The plugin, from one frame to the next, minus its GL. `params` holds what
 * the host has set, as floats, indexed by PT; Update Now is an event and
 * goes through setParam, as SetFloatParameter does.
 */
export class Engine {
  constructor() {
    this.params = new Float32Array(PT.ABOUT_TEXT);
    for (const d of DECLARED) if (d.index < PT.ABOUT_TEXT && d.type !== 'event' && d.type !== 'buffer') this.params[d.index] = d.default;
    this.sign = new Sign();
    this.luma = new Float32Array(0);
    this.target = new Uint8Array(0);
    this.lastSeconds = -1.0;
    this.firstFrame = true;
    this.onset = new Onset();
    this.onsetFired = false;
    this.fireThisFrame = false;
    this.updatePending = false;
    this.nextTick = -1.0;
    this.lastUpdateMode = -1;
    this.pictureWidth = 0;
    this.pictureHeight = 0;
    this.turn = new Float32Array(0);
    this.fires = 0;
  }

  /** SetFloatParameter. */
  setParam(index, value) {
    if (index === PT.UPDATE_NOW) {
      if (f32(value) >= 0.5) this.updatePending = true;
      return;
    }
    this.params[index] = value;
  }

  /** InitGL's resets: the sign itself is NOT reset (bistable across a retrigger). */
  initGL() {
    this.firstFrame = true;
    this.lastSeconds = -1.0;
    this.onset.reset();
    this.nextTick = -1.0;
    this.lastUpdateMode = -1;
  }

  decideUpdate(now) {
    const mode = optionIndex(this.params[PT.UPDATE], K_UPDATE_COUNT);
    let fire = this.updatePending;
    this.updatePending = false;
    if (mode !== this.lastUpdateMode) {
      this.lastUpdateMode = mode;
      this.nextTick = -1.0;
    }
    switch (mode) {
      case K_UPDATE_INTERVAL: {
        const interval = intervalFromParam(this.params[PT.INTERVAL]);
        if (this.firstFrame) fire = true;
        if (this.nextTick < 0.0) this.nextTick = now + interval;
        else if (now >= this.nextTick) {
          fire = true;
          this.nextTick += interval;
          if (now >= this.nextTick) this.nextTick = now + interval;
        }
        break;
      }
      case K_UPDATE_ONSET:
        if (this.onsetFired) fire = true;
        break;
      default:
        break;
    }
    this.fireThisFrame = fire;
  }

  /**
   * One ProcessOpenGL. `now` is the host clock in seconds; the picture and the
   * viewport are in pixels. `readMeans(uniforms, columns, rows)` runs the means
   * pass and returns its read-back (a Float32Array, row 0 the top); it is
   * called only when the plugin would run that pass. Returns the uniforms of
   * both passes and the cos/sin upload.
   */
  frame(now, pictureWidth, pictureHeight, viewportWidth, viewportHeight, readMeans, bins = null) {
    const P = this.params;
    let dt = 0.0;
    if (this.lastSeconds >= 0.0) dt = Math.min(Math.max(now - this.lastSeconds, 0.0), K_MAX_FRAME_DELTA);
    this.lastSeconds = now;

    this.onsetFired = this.onset.frame(now, bins, bins ? K_AUDIO_BINS : 0);
    this.decideUpdate(now);
    const mode = optionIndex(P[PT.UPDATE], K_UPDATE_COUNT);
    const continuous = mode === K_UPDATE_CONTINUOUS;

    const columns = intParam(P[PT.COLUMNS], K_COLUMNS_MIN, K_COLUMNS_MAX);
    const rows = intParam(P[PT.ROWS], K_ROWS_MIN, K_ROWS_MAX);
    const offset = optionIndex(P[PT.LAYOUT], K_LAYOUT_COUNT) === K_LAYOUT_OFFSET;
    this.sign.configure(columns, rows, stuckFromParam(P[PT.STUCK]), lateFromParam(P[PT.LATE]));
    this.pictureWidth = pictureWidth;
    this.pictureHeight = pictureHeight;

    const inGeometry = geometryFor(pictureWidth, pictureHeight, columns, rows, offset);
    const outGeometry = geometryFor(viewportWidth, viewportHeight, columns, rows, offset);
    // std::log2 of a float is log2f; its result rounded to float.
    const pictureLod = Math.floor(f32(Math.log2(Math.max(1.0, f32(inGeometry.pitch / 8.0)))));

    const discs = columns * rows;
    let means = null;
    if (continuous || this.fireThisFrame || this.target.length !== discs) {
      means = {
        Grid: [columns, rows],
        OffsetRows: offset ? 1 : 0,
        BoardOrigin: [inGeometry.originX, inGeometry.originY],
        Pitch: inGeometry.pitch,
        PictureSize: [pictureWidth, pictureHeight],
        PictureLod: pictureLod,
      };
      this.luma = readMeans(means, columns, rows);
      this.target = ditherGrid(this.luma, columns, rows, optionIndex(P[PT.DITHER], K_DITHER_COUNT), P[PT.THRESHOLD]);
    }

    const settings = {
      continuous,
      scanRows: optionIndex(P[PT.SCAN], K_SCAN_COUNT) === K_SCAN_ROWS,
      scanRate: scanRateFromParam(P[PT.SCAN_RATE]),
      onlyChanges: P[PT.ONLY_CHANGES] > 0.5,
      flipTime: flipTimeFromParam(P[PT.FLIP_TIME]),
      restitution: reboundFromParam(P[PT.REBOUND]),
    };
    this.sign.advance(dt, this.fireThisFrame, this.target, settings);
    this.firstFrame = false;
    if (this.fireThisFrame) this.fires += 1;

    // uploadAngles: cos and sin in double, of the float angle, stored as float.
    const angles = this.sign.angles;
    if (this.turn.length !== angles.length * 2) this.turn = new Float32Array(angles.length * 2);
    for (let i = 0; i < angles.length; i += 1) {
      const phi = angles[i];
      this.turn[2 * i] = Math.cos(phi);
      this.turn[2 * i + 1] = Math.sin(phi);
    }

    const colour = optionIndex(P[PT.COLOUR], K_COLOUR_COUNT);
    const light = lightAngleFromParam(P[PT.LIGHT]);
    const board = {
      Grid: [columns, rows],
      OffsetRows: offset ? 1 : 0,
      BoardOrigin: [outGeometry.originX, outGeometry.originY],
      BoardSize: [outGeometry.width, outGeometry.height],
      Pitch: outGeometry.pitch,
      OutSize: [f32(viewportWidth), f32(viewportHeight)],
      DiscSize: discSizeFromParam(P[PT.DISC_SIZE]),
      FaceColour: FACES[colour],
      LightSinCos: [f32(Math.sin(light)), f32(Math.cos(light))],
      MixAmount: P[PT.MIX],
    };
    return { dt, mode, continuous, columns, rows, discs, means, board, turn: this.turn, settings, fired: this.fireThisFrame };
  }
}
