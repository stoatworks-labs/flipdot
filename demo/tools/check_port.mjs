/**
 * The demo's CPU half (demo/sign.js) against the plugin's own C++.
 *
 *     demo/tools/check_port.sh          (finds node and c++, runs this)
 *     node demo/tools/check_port.mjs
 *
 * Receipt's check (demo/tools/check_port.mjs there), for a plugin whose sign,
 * dither and driver live on the CPU.
 *
 * ------------------------------------------------------------ the reference
 *
 * refsign.cpp is compiled against source/Sign.cpp, Disc.cpp, Dither.cpp,
 * Controls.cpp, Onset.cpp and Clock.cpp UNCHANGED, and against text this
 * script CUTS OUT of the plugin's source at run time:
 *
 *   from Flipdot.h    the Debug struct and the whole private section (every
 *                     member, and the declarations of uploadAngles and
 *                     decideUpdate)
 *   from Flipdot.cpp  the anonymous namespace after its static_assert (the
 *                     option names, the faces, the frame clamp, intParam,
 *                     Geometry and geometryFor); the constructor; decideUpdate,
 *                     uploadAngles and ProcessOpenGL; SetFloatParameter; SetTime
 *
 * against stand-ins for FFGL, GL and ffglex (demo/tools/stub/FFGLSDK.h). So
 * the declarations, the clock's delta and its clamp, the update decision, the
 * regrid, when the means pass runs, the dither, the driver, every disc, the
 * cos/sin upload and every uniform both passes are handed come from the
 * plugin's own text. A marker that is not found fails the check rather than
 * falling back to anything.
 *
 * It is built twice. Once with -ffp-contract=off, the arithmetic JavaScript
 * does (no fused multiply-add, ever), which must agree EXACTLY. And once with
 * the plugin's own flags (CMake's Release: -O3, clang's default contraction),
 * which on this Mac's arm64 may fuse `a + b * c` into one rounding. That
 * second build is measured, not required: it says how far the arm64 slice of
 * the shipped bundle can sit from this page, in ulps, on the same cases.
 *
 * ------------------------------------------------------------ what it compares
 *
 *   params   every parameter the constructor declares -- index, name, FFGL
 *            type, group, default, range, option elements -- against sign.js's
 *            DECLARED, which the page's inspector is built from
 *   laws     every control law and its inverse, OptionIndex, intParam and the
 *            tone curve at 1,012 host values; the disc profile (SwingAngle,
 *            ReboundDepth, ReboundEnd) at 4,001 points for nine restitutions, two of them
 *            a hair under the half-degree cut;
 *            geometryFor at 1,440 sizes: exactly
 *   frames   the page's Engine against the plugin's ProcessOpenGL, frame by
 *            frame through the cases below: whether the means pass ran, the
 *            fire, the grid, the stuck count, whether a pass is active, a
 *            64-bit hash of every disc's float angle, of the target bits and
 *            of the whole cos/sin upload, and every uniform of both passes,
 *            exactly; and at the end of each case the whole upload, float for
 *            float
 *
 * ------------------------------------------------------------ what it cannot
 *
 * The means are this script's, written to files both sides read: the means
 * pass that makes them from a clip is the plugin's GLSL on a GPU and is not in
 * this check (check_shaders.py holds the page's copy of it to the plugin's),
 * and neither is the board pass that draws the upload. refsign is not the
 * plugin binary and has no GL: that the uniforms reach the shaders, and that
 * plugin.js's GL calls match the plugin's, only a reader checks. Onset is fed
 * silence on both sides, as the page feeds it. The cases are the cases below;
 * agreement on them is evidence about the port, not a proof of it.
 */
import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = join(HERE, '..', '..');
const S = await import(join(REPO, 'demo', 'sign.js'));

let problems = 0;
const fail = (what) => {
  problems += 1;
  console.log(`FAIL  ${what}`);
};
const ok = (what) => console.log(`ok    ${what}`);

const scratch = mkdtempSync(join(tmpdir(), 'flipdot-port-'));
process.on('exit', () => rmSync(scratch, { recursive: true, force: true }));

//---------------------------------------------------------------------------
// Cut the plugin's own text.
//---------------------------------------------------------------------------
function cut(text, start, end, what, { includeEnd = true } = {}) {
  const a = text.indexOf(start);
  if (a < 0) throw new Error(`${what}: start marker not found: ${JSON.stringify(start)}`);
  const b = text.indexOf(end, a + start.length);
  if (b < 0) throw new Error(`${what}: end marker not found: ${JSON.stringify(end)}`);
  return text.slice(a, includeEnd ? b + end.length : b);
}

const header = readFileSync(join(REPO, 'source', 'Flipdot.h'), 'utf8');
const body = readFileSync(join(REPO, 'source', 'Flipdot.cpp'), 'utf8');
try {
  const debug = cut(header, '\t/// Negative controls: each breaks one claim so a check can be seen to fail.\n\tstruct Debug', '\t};\n', 'Debug');
  const members = cut(header, 'private:\n', '\n};', 'private section', { includeEnd: false });
  const names = cut(body, 'const char* const kLayoutNames', '} // namespace\n', 'anonymous namespace', { includeEnd: false });
  const ctor = cut(body, 'FlipdotPlugin::FlipdotPlugin()', '\n}\n', 'constructor');
  const process_ = cut(body, 'void FlipdotPlugin::decideUpdate( double now )', '//---------------------------------------------------------------------------\nFFResult FlipdotPlugin::DeInitGL()', 'decideUpdate..ProcessOpenGL', { includeEnd: false });
  const setFloat = cut(body, 'FFResult FlipdotPlugin::SetFloatParameter( unsigned int index, float value )', '\n}\n', 'SetFloatParameter');
  const setTime = cut(body, 'FFResult FlipdotPlugin::SetTime( double time )', '\n}\n', 'SetTime');
  writeFileSync(join(scratch, 'cut_debug.inc'), `${debug}\n`);
  writeFileSync(join(scratch, 'cut_class.inc'), `${members}\n`);
  writeFileSync(join(scratch, 'cut_defs.inc'), `namespace\n{\n${names}} // namespace\n\n${ctor}\n${process_}\n${setFloat}\n${setTime}\n`);
} catch (error) {
  fail(`cutting the plugin's source: ${error.message}`);
  process.exit(1);
}

const sources = ['Sign.cpp', 'Disc.cpp', 'Dither.cpp', 'Controls.cpp', 'Onset.cpp', 'Clock.cpp'].map((f) => join(REPO, 'source', f));
function build(out, flags) {
  execFileSync('c++', [
    '-std=c++17', ...flags, '-Wall',
    '-I', join(HERE, 'stub'), '-I', join(REPO, 'source'), '-I', scratch,
    '-o', out, join(HERE, 'refsign.cpp'), ...sources,
  ], { stdio: ['ignore', 'pipe', 'pipe'] });
}
const refsign = join(scratch, 'refsign');
const refsignFused = join(scratch, 'refsign-release');
try {
  build(refsign, ['-O2', '-ffp-contract=off']);
  build(refsignFused, ['-O3', '-DNDEBUG']);
} catch (error) {
  console.log(String(error.stderr ?? error.message));
  fail('refsign did not build against the cut source');
  process.exit(1);
}
ok(`refsign built from ${sources.length} source files unchanged and text cut from Flipdot.h and Flipdot.cpp (twice: -ffp-contract=off, and the Release flags)`);

const run = (bin, ...args) => execFileSync(bin, args, { maxBuffer: 1 << 28 }).toString();
const num = (s) => Number(s);

//---------------------------------------------------------------------------
// params
//---------------------------------------------------------------------------
{
  const lines = run(refsign, 'params').trim().split('\n');
  const declared = new Map(S.DECLARED.map((d) => [d.index, d]));
  let compared = 0;
  let about = 0;
  for (const line of lines) {
    const [head, name, type, group, def, range, elements] = line.split('|');
    const index = Number(head.split(' ')[1]);
    if (index >= S.PT.ABOUT_TEXT) {
      about += 1;
      if (group !== 'About') fail(`param ${index} (${name}) is past the About line but in group ${group}`);
      continue;
    }
    const d = declared.get(index);
    if (!d) { fail(`param ${index} ${name}: declared by the plugin, missing from sign.js DECLARED`); continue; }
    const mine = [d.name, d.type, d.group, String(d.default),
      d.range ? d.range.join(',') : '',
      d.type === 'option' ? d.elements.map((e, i) => `${e}=${i}`).join(';') : d.type === 'buffer' ? `${d.bins} bins` : ''];
    const theirs = [name, type, group, String(num(def)), range ? range.split(',').map(num).join(',') : '', elements.replace(/=(\S+?)(;|$)/g, (_m, v, sep) => `=${num(v)}${sep}`)];
    if (mine.join('|') !== theirs.join('|')) fail(`param ${index}:\n        C++: ${theirs.join('|')}\n        js : ${mine.join('|')}`);
    compared += 1;
    declared.delete(index);
  }
  for (const d of declared.values()) fail(`param ${d.index} ${d.name}: in sign.js DECLARED, not declared by the plugin`);
  if (compared === S.DECLARED.length) ok(`params: all ${compared} declarations identical to the constructor's (name, type, group, default, range, elements), plus ${about} About entries the page does not draw`);
}

//---------------------------------------------------------------------------
// laws
//---------------------------------------------------------------------------
function lawsJs(v) {
  return [
    S.optionIndex(v, 2), S.optionIndex(v, 3), S.optionIndex(v, 4),
    S.intParam(Math.fround(v * 200), S.K_COLUMNS_MIN, S.K_COLUMNS_MAX),
    S.discSizeFromParam(v), S.scanRateFromParam(v), S.intervalFromParam(v), S.flipTimeFromParam(v),
    S.reboundFromParam(v), S.stuckFromParam(v), S.lateFromParam(v), S.lightAngleFromParam(v),
    S.scanRateToParam(S.scanRateFromParam(v)), S.intervalToParam(S.intervalFromParam(v)), S.flipTimeToParam(S.flipTimeFromParam(v)),
    S.reboundToParam(S.reboundFromParam(v)), S.stuckToParam(S.stuckFromParam(v)), S.lateToParam(S.lateFromParam(v)),
    S.toneCurve(v, 0.25), S.toneCurve(v, 0.5), S.toneCurve(v, 0.0), S.toneCurve(v, v),
  ];
}

function hashBytes(bytes) {
  let a = 2166136261;
  for (let i = 0; i < bytes.length; i += 1) a = Math.imul(a ^ bytes[i], 16777619) >>> 0;
  let b = 0x9747b28c;
  for (let i = 0; i < bytes.length; i += 4) {
    const w = (bytes[i] | ((bytes[i + 1] ?? 0) << 8) | ((bytes[i + 2] ?? 0) << 16) | ((bytes[i + 3] ?? 0) << 24)) >>> 0;
    b = (Math.imul(b, 31) + w) >>> 0;
  }
  return a.toString(16).padStart(8, '0') + b.toString(16).padStart(8, '0');
}
const hashOf = (typed) => hashBytes(new Uint8Array(typed.buffer, typed.byteOffset, typed.byteLength));

function compareLaws(lines, label) {
  let laws = 0, profiles = 0, geometries = 0;
  const bad = [];
  for (const line of lines) {
    const w = line.split(' ');
    if (w[0] === 'law') {
      laws += 1;
      const v = Math.fround(num(w[1]));
      const js = lawsJs(v);
      const cpp = w.slice(2).map(num);
      for (let i = 0; i < js.length; i += 1) {
        if (!Object.is(js[i], cpp[i])) { bad.push(`law ${i} at v = ${w[1]}: js ${js[i]}, C++ ${cpp[i]}`); break; }
      }
    } else if (w[0] === 'profile') {
      profiles += 1;
      const e = num(w[1]);
      const samples = new Float64Array(8002);
      for (let k = 0; k <= 4000; k += 1) {
        const u = (k - 250) / 1000;
        samples[2 * k] = S.swingAngle(u, e);
        samples[2 * k + 1] = S.reboundDepth(u, e);
      }
      const end = S.reboundEnd(e);
      const h = hashOf(samples);
      if (!Object.is(end, num(w[2])) || h !== w[3]) bad.push(`profile at e = ${e}: js end ${end} ${h}, C++ ${w[2]} ${w[3]}`);
    } else if (w[0] === 'geometry') {
      geometries += 1;
      const [pw, ph, c, r, off] = w.slice(1, 6).map(num);
      const g = S.geometryFor(pw, ph, c, r, off === 1);
      const js = [g.pitch, g.originX, g.originY, g.width, g.height];
      const cpp = w.slice(6).map(num);
      if (js.some((x, i) => !Object.is(x, cpp[i]))) bad.push(`geometry ${w.slice(1, 6).join(' ')}: js ${js.join(' ')}, C++ ${cpp.join(' ')}`);
    }
  }
  return { laws, profiles, geometries, bad, label };
}

{
  const r = compareLaws(run(refsign, 'laws').trim().split('\n'));
  if (r.laws !== 1012 || r.profiles !== 9 || r.geometries !== 1440) fail(`laws: ${r.laws} law, ${r.profiles} profile and ${r.geometries} geometry lines from refsign`);
  for (const b of r.bad.slice(0, 8)) fail(b);
  if (r.bad.length > 8) fail(`... and ${r.bad.length - 8} more`);
  if (!r.bad.length) ok(`laws: 22 laws at ${r.laws.toLocaleString('en-GB')} host values, the disc profile at 4,001 points for ${r.profiles} restitutions, geometryFor at ${r.geometries.toLocaleString('en-GB')} sizes, all identical`);
  const rf = compareLaws(run(refsignFused, 'laws').trim().split('\n'));
  ok(`laws, Release flags (measured, not required): ${rf.bad.length} of ${(r.laws + r.profiles + r.geometries).toLocaleString('en-GB')} lines differ from this page${rf.bad.length ? ` -- first: ${rf.bad[0]}` : ''}`);
}

//---------------------------------------------------------------------------
// means: this script's, written as float32 for both sides
//---------------------------------------------------------------------------
function meansFor(columns, rows, variant, k) {
  const out = new Float32Array(columns * rows);
  for (let r = 0; r < rows; r += 1) {
    for (let c = 0; c < columns; c += 1) {
      let v;
      if (variant === 'black') v = 0;
      else if (variant === 'white') v = 1;
      else if (variant === 'noise') v = S.hashInt((c + r * 4096 + k * 7919) >>> 0) / 4294967296;
      else if (variant === 'edges') {
        // Exactly on a Bayer level through the default curve, exactly on the
        // Threshold, and either side of both by one float.
        const band = (c + r + k) % 6;
        const level = (((c * 5 + r * 3) % 16) + 0.5) / 16;
        const onCurve = level < 0.5 ? level * 2 * 0.25 : 0.25 + (level - 0.5) * 2 * 0.75;
        v = [0.25, 0.5, onCurve, Math.fround(onCurve) + 1e-7, Math.fround(onCurve) - 1e-7, 0.1][band];
      } else {
        // 'scene': a moving blob on a slow gradient.
        const x = c / Math.max(columns - 1, 1);
        const y = r / Math.max(rows - 1, 1);
        const cx = 0.5 + 0.35 * Math.sin(k * 0.07);
        const cy = 0.5 + 0.3 * Math.cos(k * 0.05);
        const d = Math.hypot(x - cx, (y - cy) * rows / columns);
        v = Math.min(1, Math.max(0, 0.15 * x + (d < 0.22 ? 0.9 : 0) + 0.05 * Math.sin(20 * y + k * 0.3)));
      }
      out[r * columns + c] = v;
    }
  }
  return out;
}

const meansFiles = new Map();
function meansFile(columns, rows, variant, k) {
  const key = `${columns}x${rows}-${variant}-${k}`;
  if (!meansFiles.has(key)) {
    const path = join(scratch, `means-${key}.f32`);
    const data = meansFor(columns, rows, variant, k);
    writeFileSync(path, Buffer.from(data.buffer));
    meansFiles.set(key, { path, data });
  }
  return meansFiles.get(key);
}

//---------------------------------------------------------------------------
// frames
//---------------------------------------------------------------------------
const I = Object.fromEntries(S.DECLARED.map((d) => [d.id, d.index]));

const fmtUniforms = (u) => (u ? Object.keys(u).sort().map((k) => `${k}=${[].concat(u[k]).map((x) => String(Number(x))).join(',')}`).join(' ') : '');
const normalise = (s) => s.trim().split(' ').filter(Boolean).map((kv) => {
  const [k, v] = kv.split('=');
  return `${k}=${v.split(',').map((n) => String(Number(n))).join(',')}`;
}).join(' ');

/// Steps: { set: { id: hostValue } } or { frame: [now, pw, ph, vw, vh], variant, k }.
function frames(fps, count, start = 0, size = [1280, 720, 1280, 720], variant = 'scene', k0 = 0) {
  const out = [];
  for (let i = 0; i < count; i += 1) out.push({ frame: [start + i / fps, ...size], variant, k: k0 + i });
  return out;
}
const at = (times, size = [1280, 720, 1280, 720], variant = 'scene') => times.map((t, i) => ({ frame: [t, ...size], variant, k: i }));

const CASES = {
  'Continuous, the defaults, 1280x720, a moving clip': [...frames(60, 120)],
  'Continuous, Threshold then Floyd-Steinberg, means on the dither edges': [
    { set: { dither: 0, threshold: 0.5 } }, ...frames(60, 20, 0, undefined, 'edges'),
    { set: { dither: 2 } }, ...frames(60, 20, 1, undefined, 'noise'),
    { set: { threshold: 0.1 } }, ...frames(60, 20, 2, undefined, 'scene'),
    { set: { dither: 1, threshold: 0.25 } }, ...frames(60, 20, 3, undefined, 'edges'),
  ],
  'Continuous to Manual mid-sweep; Refresh All, full rebound, one-second swings, row by row, offset rows': [
    { set: { columns: 16, rows: 9, scanRate: 0.3 } }, ...frames(60, 10),
    { set: { update: 3, onlyChanges: 0, rebound: 1, flipTime: 1, scan: 1, layout: 1 } }, ...frames(60, 30, 0.2),
    { set: { updateNow: 1 } }, ...frames(60, 90, 0.8, undefined, 'white'),
    { set: { updateNow: 1 } }, ...frames(60, 3, 2.3, undefined, 'noise'),
    { set: { updateNow: 1 } }, ...frames(60, 3, 2.35, undefined, 'black'),
    { set: { updateNow: 1 } },
    ...at([2.4, 2.41, 2.45, 2.9, 2.9, 3.3, 3.31, 4.0, 4.5, 5.2, 6.0, 7.5], [1280, 720, 1280, 720], 'scene'),
  ],
  'Interval, stalls, a scrub and the clock going backwards': [
    { set: { update: 1, interval: 0, flipTime: 0.3 } },
    ...at([0, 0.016, 0.05, 0.12, 0.2, 0.2, 0.35, 0.5, 3.5, 3.52, 3.6, 3.7, 1.0, 1.02, 1.2, 1.4, 1.41, 1.6, 2.0]),
    { set: { interval: 0.5 } }, ...frames(30, 60, 2.1, undefined, 'noise'),
  ],
  'Regrid mid-pass; Stuck and Late at their maximum, then lowered': [
    { set: { scanRate: 0.2, stuck: 1, late: 1 } }, ...frames(60, 20),
    { set: { columns: 100, rows: 50 } }, ...frames(60, 20, 0.4, undefined, 'noise'),
    { set: { columns: 17, rows: 9 } }, ...frames(60, 20, 0.8, undefined, 'white'),
    { set: { stuck: 0.3, late: 0.1 } }, ...frames(60, 20, 1.2, undefined, 'black'),
    { set: { columns: 34, rows: 4.5 } }, ...frames(60, 10, 1.6, undefined, 'scene'),
    { set: { columns: 8, rows: 4 } }, ...frames(60, 5, 1.8, undefined, 'white'),
    { set: { columns: 16, rows: 2 } }, ...frames(60, 5, 1.9, undefined, 'black'),
  ],
  'Sizes: 192x108 at 1920x1080, a picture unlike the viewport, 4x2 at 320x180, odd rasters': [
    { set: { columns: 192, rows: 108 } }, ...frames(60, 8, 0, [1920, 1080, 1920, 1080]),
    ...frames(60, 4, 0.2, [1280, 720, 1920, 1080], 'noise'),
    { set: { columns: 4, rows: 2, discSize: 0, light: 1, colour: 2 } }, ...frames(60, 8, 0.3, [320, 180, 320, 180], 'white'),
    { set: { columns: 23, rows: 13, layout: 1, discSize: 1, light: 0, colour: 1, mix: 0.37 } }, ...frames(60, 8, 0.5, [721, 405, 721, 405]),
    ...frames(60, 4, 0.7, [3840, 2160, 640, 360], 'edges'),
  ],
  'Scan rates and flip times at the ends of their ranges': [
    { set: { scanRate: 0, flipTime: 0 } }, ...frames(60, 40, 0, undefined, 'noise'),
    { set: { scanRate: 1 } }, ...frames(60, 20, 1, undefined, 'white'),
    { set: { scanRate: 0.999, flipTime: 0.999, rebound: 0.999 } }, ...frames(24, 30, 2, undefined, 'black'),
    { set: { flipTime: 0.5, rebound: 0.5 } }, ...frames(24, 30, 4, undefined, 'scene'),
  ],
  'Onset on silence, Update Now in Onset, Only Changes off in Continuous': [
    { set: { update: 2 } }, ...frames(60, 30, 0, undefined, 'white'),
    { set: { updateNow: 1 } }, ...frames(60, 30, 0.5, undefined, 'white'),
    { set: { update: 0, onlyChanges: 0, rebound: 0.8 } }, ...frames(60, 40, 1, undefined, 'noise'),
    { set: { rebound: 0 } }, ...frames(60, 20, 2, undefined, 'noise'),
  ],
};

function runCases(bin) {
  const results = {};
  let frameCount = 0;
  let upload = 0;
  const failures = [];
  for (const [name, steps] of Object.entries(CASES)) {
    const engine = new S.Engine();
    const script = [];
    const expected = [];
    for (const step of steps) {
      if (step.set) {
        for (const [id, v] of Object.entries(step.set)) {
          engine.setParam(I[id], Math.fround(v));
          script.push(`set ${I[id]} ${Math.fround(v)}`);
        }
        continue;
      }
      const [now, pw, ph, vw, vh] = step.frame;
      const columns = S.intParam(engine.params[S.PT.COLUMNS], S.K_COLUMNS_MIN, S.K_COLUMNS_MAX);
      const rows = S.intParam(engine.params[S.PT.ROWS], S.K_ROWS_MIN, S.K_ROWS_MAX);
      const means = meansFile(columns, rows, step.variant, step.k);
      let reads = 0;
      const out = engine.frame(now, pw, ph, vw, vh, () => { reads += 1; return means.data; });
      const sign = engine.sign;
      expected.push({
        line: `result=0 read=ok means=${reads} fired=${out.fired ? 1 : 0} grid=${sign.columns}x${sign.rows} stuck=${sign.stuckCount} active=${sign.active ? 1 : 0} angles=${hashOf(sign.angles)} turn=${hashOf(out.turn)} target=${hashOf(engine.target)}`,
        uniforms: `${fmtUniforms(out.means)} | ${fmtUniforms(out.board)}`,
      });
      script.push(`frame ${now} ${pw} ${ph} ${vw} ${vh} ${means.path}`);
    }
    script.push('dump final.f32');
    const scriptPath = join(scratch, 'script.txt');
    writeFileSync(scriptPath, `${script.join('\n')}\n`);
    const lines = run(bin, 'frames', scriptPath, scratch).trim().split('\n');

    let bad = null;
    let differing = 0;
    if (lines.length !== expected.length) bad = `${lines.length} frames from refsign, ${expected.length} here`;
    for (let i = 0; i < expected.length && lines.length === expected.length; i += 1) {
      const m = /^frame (\d+) (.*?) \| (.*?)\| (.*)$/.exec(lines[i]);
      if (!m) { bad = `frame ${i}: cannot read refsign's line: ${lines[i]}`; break; }
      const cppUniforms = `${normalise(m[3])} | ${normalise(m[4])}`;
      const jsUniforms = expected[i].uniforms.split(' | ').map(normalise).join(' | ');
      if (m[2] !== expected[i].line || cppUniforms !== jsUniforms) {
        differing += 1;
        if (!bad) bad = m[2] !== expected[i].line
          ? `frame ${i}:\n        C++: ${m[2]}\n        js : ${expected[i].line}`
          : `frame ${i} uniforms:\n        C++: ${cppUniforms}\n        js : ${jsUniforms}`;
      }
    }
    // The last upload, float for float, and how far apart in ulps if not.
    const finalBytes = readFileSync(join(scratch, 'final.f32'));
    const final = new Float32Array(finalBytes.buffer.slice(finalBytes.byteOffset, finalBytes.byteOffset + finalBytes.length));
    const mine = engine.turn;
    let floatsDiffer = 0;
    let worstUlp = 0;
    if (final.length !== mine.length) bad = bad ?? `final upload: ${final.length} floats in C++, ${mine.length} here`;
    else {
      const a = new Int32Array(final.buffer);
      const b = new Int32Array(mine.buffer, mine.byteOffset, mine.length);
      for (let i = 0; i < final.length; i += 1) {
        if (!Object.is(final[i], mine[i])) {
          floatsDiffer += 1;
          worstUlp = Math.max(worstUlp, Math.abs(a[i] - b[i]));
          if (!bad) bad = `final upload float ${i} (disc ${i >> 1} ${i & 1 ? 'sin' : 'cos'}): js ${mine[i]}, C++ ${final[i]}`;
        }
      }
    }
    upload += final.length;
    frameCount += expected.length;
    results[name] = { frames: expected.length, differing, floatsDiffer, worstUlp, floats: final.length, bad };
    if (bad) failures.push(`${name}: ${bad}`);
  }
  return { results, frameCount, upload, failures };
}

const exact = runCases(refsign);
for (const [name, r] of Object.entries(exact.results)) {
  if (r.differing || r.floatsDiffer || r.bad) fail(`${name}: ${r.differing} of ${r.frames} frames differ, ${r.floatsDiffer} of ${r.floats} final floats`);
  else ok(`${name}: ${r.frames} frames and the final ${r.floats.toLocaleString('en-GB')} upload floats identical`);
}
for (const f of exact.failures) console.log(`        ${f.replace(/\n/g, '\n        ')}`);

const release = runCases(refsignFused);
{
  let frames = 0, differing = 0, floats = 0, worst = 0;
  for (const r of Object.values(release.results)) {
    frames += r.frames;
    differing += r.differing;
    floats += r.floatsDiffer;
    worst = Math.max(worst, r.worstUlp);
  }
  ok(`frames, Release flags (measured, not required): ${differing} of ${frames} frames differ from this page; ${floats} of ${release.upload.toLocaleString('en-GB')} final upload floats, worst ${worst} ulp${release.failures.length ? ` -- first: ${release.failures[0].split('\n')[0]}` : ''}`);
}

console.log();
if (problems) {
  console.log(`${problems} difference(s) between demo/sign.js and the plugin's C++`);
  process.exit(1);
}
console.log(`sign.js agrees with the plugin's C++ exactly: ${S.DECLARED.length} declarations, the laws, and ${exact.frameCount} frames of ProcessOpenGL (${exact.upload.toLocaleString('en-GB')} final upload floats)`);
