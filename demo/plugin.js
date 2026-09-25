/**
 * Flipdot — browser demo.
 *
 * The picture on an electromagnetic flip-dot sign. The one idea, from
 * `AGENTS.md`: a disc stays where it is with no power, and the driver reaches
 * it one line at a time. A whole-sign change sweeps across at Scan Rate lines
 * a second, a disc takes Flip Time to swing and rebounds off its stop, only
 * what changes is pulsed (or, with Refresh All, everything), one bit a disc,
 * and an old sign has stuck discs and weak coils.
 *
 * Like receipt and splitflap, this plugin is **not only a shader**, and the
 * two halves of the page are not equally faithful:
 *
 *   The shaders are the plugin's. The four GLSL programs below -- vertex,
 *   copy, means and board -- are `source/Shaders.cpp`'s string constants,
 *   copied across unedited by `demo/tools/splice_shaders.py`.
 *   `demo/tools/check_shaders.py` compares them character for character and
 *   `tools/verify.sh` runs it. Here they run in WebGL2: the copy into a
 *   mipmapped texture, the means pass into an R32F target one texel per disc,
 *   read back, and the board drawn from one cos and one sin per disc.
 *
 *   The CPU half is a PORT, in `demo/sign.js`: `Sign.cpp` (every disc's state
 *   and the driver, in double), `Disc.cpp` (the stated swing and rebound),
 *   `Dither.cpp` (the tone curve and the three dithers, after the
 *   read-back), `Controls.cpp`, `Onset.cpp`, and the frame sequence of
 *   `FlipdotPlugin::ProcessOpenGL` with `decideUpdate` and `uploadAngles`.
 *   `demo/tools/check_port.sh` builds the plugin's own C++ from those files
 *   and text cut from Flipdot.cpp and compares sign.js with it, frame by
 *   frame, on its own cases; `tools/verify.sh` runs it. Nothing checks the
 *   copy of sign.js your browser is running but the arithmetic itself.
 *
 * ------------------------------------------------------------- what is missing
 *
 * **Nothing audio.** The Audio FFT buffer is absent -- it is a host-written
 * buffer, not a control -- and the ported onset detector is handed silence
 * every frame, which it reads exactly as the plugin reads an unrouted input.
 * Onset mode therefore never fires here, and nothing fakes an onset. **Update
 * Now is an event**, a button here. **Columns and Rows are FF_TYPE_INTEGER**,
 * dropdowns here. **The About block is absent**, as on every page in this
 * suite. **Performance is not claimed**: the plugin stalls on the read-back
 * once a frame in Continuous, and how long that takes inside Resolume is
 * unmeasured.
 */

import { mountDemo } from './vendor/demo.js';
import { Program, PassBuffer, bindTexture } from './vendor/gl.js';
import * as S from './sign.js';

//---------------------------------------------------------------------------
// Shaders — verbatim from source/Shaders.cpp, written here by
// demo/tools/splice_shaders.py. Do not edit between the markers.
//---------------------------------------------------------------------------

// @@shaders-begin -- written by demo/tools/splice_shaders.py, do not edit

const VERTEX = `#version 410 core

layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv = vUV;
}
`;

const COPY = `#version 410 core

uniform sampler2D InputTexture;
uniform vec2 MaxUV;

in vec2 uv;
out vec4 fragColor;

void main()
{
	fragColor = texture( InputTexture, uv * MaxUV );
}
`;

const MEANS = `#version 410 core

uniform sampler2D Picture;    //the clip, mipmapped
uniform ivec2 Grid;
uniform int OffsetRows;       //1: odd rows sit half a pitch to the right
uniform vec2 BoardOrigin;     //the sign's top-left corner, in pixels from the picture's top-left
uniform float Pitch;          //pixels from one disc to the next
uniform vec2 PictureSize;     //pixels
uniform float PictureLod;     //the whole mip level whose texel is at most an eighth of the pitch

out vec4 fragColor;

//The picture uv of a point in disc (c, r), offset in pitch units from its
//centre (x right, y down).
vec2 discUV( ivec2 disc, vec2 offset )
{
	float shift = ( OffsetRows == 1 && ( disc.y & 1 ) == 1 ) ? 0.5 : 0.0;
	vec2 px = BoardOrigin + ( vec2( disc ) + vec2( 0.5 + shift, 0.5 ) + offset ) * Pitch;
	return vec2( px.x / PictureSize.x, 1.0 - px.y / PictureSize.y );
}

void main()
{
	ivec2 disc = ivec2( gl_FragCoord.xy );

	//Sixteen bilinear taps at +-1/8 and +-3/8 of the pitch, at a level whose
	//texel is no wider than an eighth of it: each tap's footprint reaches the
	//cell's edge and stops, so the mean is this disc's and not a neighbour's.
	vec4 sum = vec4( 0.0 );
	for( int j = 0; j < 4; ++j )
		for( int i = 0; i < 4; ++i )
			sum += textureLod( Picture, discUV( disc, vec2( -0.375 + 0.25 * float( i ), -0.375 + 0.25 * float( j ) ) ), PictureLod );
	vec4 m = sum / 16.0;

	//Where the clip is transparent the sign sees black.
	float luma = dot( m.rgb, vec3( 0.2126, 0.7152, 0.0722 ) ) * m.a;
	fragColor = vec4( luma, 0.0, 0.0, 1.0 );
}
`;

const BOARD = `#version 410 core

uniform sampler2D Picture;    //the clip, for Mix
uniform sampler2D Turn;       //Columns x Rows, RG32F: cos and sin of each disc's angle from the black stop; row 0 the top
uniform ivec2 Grid;
uniform int OffsetRows;
uniform vec2 BoardOrigin;     //pixels from the output's top-left
uniform vec2 BoardSize;       //pixels
uniform float Pitch;          //pixels
uniform vec2 OutSize;         //pixels
uniform float DiscSize;       //diameter over pitch
uniform vec3 FaceColour;      //the fluorescent face
uniform vec2 LightSinCos;     //sin and cos of the light's angle: 0 from the viewer, toward grazing from the left
uniform float MixAmount;

in vec2 uv;
out vec4 fragColor;

const float kAmbient = 0.30;
const vec3 kSignFace = vec3( 0.035 );  //the sign's matte black front
const vec3 kHole = vec3( 0.012 );      //the recess each disc sits in
const vec3 kBlackFace = vec3( 0.080 ); //the disc's black side
const vec3 kRimPaint = vec3( 0.120 );  //the disc's edge

//Signed distance in pixels to an ellipse of semi-axes a, to first order.
//Exact on the horizontal through the centre, which is where --rotation
//measures the disc's width.
float ellipseDistance( vec2 p, vec2 a )
{
	vec2 q = p / a;
	float k = length( q );
	float g = length( p / ( a * a ) );
	return ( k - 1.0 ) * k / max( g, 1.0e-6 );
}

float coverage( float d )
{
	return clamp( 0.5 - d, 0.0, 1.0 );
}

void main()
{
	vec4 source = texture( Picture, uv );

	//Pixels from the output's top-left, pixel centres at .5.
	vec2 px = vec2( gl_FragCoord.x, OutSize.y - gl_FragCoord.y );
	vec2 b = px - BoardOrigin;

	vec4 board = vec4( 0.0 );
	if( all( greaterThanEqual( b, vec2( 0.0 ) ) ) && all( lessThan( b, BoardSize ) ) )
	{
		vec3 colour = kSignFace;
		vec2 g = b / Pitch;
		int row = min( int( floor( g.y ) ), Grid.y - 1 );
		float shift = ( OffsetRows == 1 && ( row & 1 ) == 1 ) ? 0.5 : 0.0;
		float gx = g.x - shift;
		int col = int( floor( gx ) );
		if( gx >= 0.0 && col < Grid.x )
		{
			//Pixels from the disc's centre, x right, y down.
			vec2 p = ( vec2( gx, g.y ) - vec2( float( col ), float( row ) ) - 0.5 ) * Pitch;
			float r = 0.5 * DiscSize * Pitch;

			//cos and sin come from the CPU in double: GLSL leaves the
			//precision of its own trigonometry to the implementation.
			vec2 turn = texelFetch( Turn, ivec2( col, row ), 0 ).rg;
			float c = turn.x;
			float s = turn.y;

			//The recess, a little wider than the disc.
			float rh = r + max( 0.05 * r, 1.0 );
			colour = mix( colour, kHole, coverage( length( p ) - rh ) );

			//Light: toward the light, from the left of the sign at LightAngle
			//off the viewer's axis. The hole's rim shades the far side of the
			//disc from an oblique light: the lit side.
			vec3 toLight = vec3( -LightSinCos.x, 0.0, LightSinCos.y );
			float lee = 1.0 - 0.45 * LightSinCos.x * smoothstep( 0.1, 1.0, p.x / r );

			//The plate turned by phi about a vertical axle: the black face's
			//normal is ( sin phi, 0, cos phi ). The face toward the viewer is
			//the black one while cos phi >= 0, the colour one after.
			float w = r * abs( c );
			float t = max( 0.06 * r, 0.75 );
			float wr = w + t * abs( s );

			float rimLight = kAmbient + ( 1.0 - kAmbient ) * LightSinCos.x * 0.8;
			colour = mix( colour, kRimPaint * rimLight * lee, coverage( ellipseDistance( p, vec2( max( wr, 1.0e-4 ), r ) ) ) );

			if( w > 1.0e-4 )
			{
				float facing = c >= 0.0 ? 1.0 : -1.0;
				vec3 normal = facing * vec3( s, 0.0, c );
				float shade = kAmbient + ( 1.0 - kAmbient ) * max( 0.0, dot( normal, toLight ) );
				vec3 paint = c >= 0.0 ? kBlackFace : FaceColour;
				colour = mix( colour, paint * shade * lee, coverage( ellipseDistance( p, vec2( w, r ) ) ) );
			}
		}
		board = vec4( colour, 1.0 );
	}

	fragColor = mix( source, board, MixAmount );
}
`;

// @@shaders-end

//===========================================================================
// The renderer: FlipdotPlugin::ProcessOpenGL's GL, in its order, around the
// ported Engine (sign.js), which does everything between the read-back and
// the upload.
//
//   1. copy    W x H, RGBA8, mip chain      the clip, into a texture of ours
//   2. means   Columns x Rows, R32F         one fragment per disc, read back
//      (CPU)   dither, driver, discs        sign.js
//   3. upload  Columns x Rows, RG32F        cos and sin of every disc's angle
//   4. board   onto the canvas
//===========================================================================

/// What the line under the canvas reports. Filled by the renderer.
const telemetry = { columns: 0, rows: 0, stuck: 0, mode: 0, fires: 0, active: false, scanRate: 0, flipTime: 0, readBack: false, dt: 0, frames: 0 };
/// For a headless driver: called after the board is drawn.
const hooks = { afterRender: null };

/// The page's controls are the plugin's declarations, minus the Audio buffer.
const ON_PAGE = S.DECLARED.filter((d) => d.page !== false);

/// A page value (an integer's dropdown index, or a host value) to what the
/// host would hold, as a float.
function hostValue(d, v) {
  if (d.type === 'integer') return Math.fround(d.range[0] + Math.round(v));
  return Math.fround(v);
}

function createRenderer(gl, quad) {
  const shaders = {
    copy: new Program(gl, VERTEX, COPY, 'copy'),
    means: new Program(gl, VERTEX, MEANS, 'means'),
    board: new Program(gl, VERTEX, BOARD, 'board'),
  };

  // The clip, ours, with a mip chain the means pass reads at a whole level.
  // The plugin's PassBuffer::Sampling::Mipmapped is trilinear too.
  const copy = new PassBuffer(gl, { filter: 'linear', mip: true });
  // One R32F texel per disc, nearest, as in the plugin.
  const means = new PassBuffer(gl, { filter: 'nearest' });

  const turnTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, turnTexture);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.bindTexture(gl.TEXTURE_2D, null);
  let turnColumns = 0, turnRows = 0;

  const engine = new S.Engine();
  let readBuffer = new Float32Array(0);

  const setIVec2 = (program, name, x, y) => {
    const loc = program.location(name);
    if (loc !== null) gl.uniform2i(loc, x, y);
  };

  /// The means pass and its read-back, called by the Engine only when the
  /// plugin would run it.
  function readMeans(u, columns, rows) {
    means.ensure(columns, rows, gl.R32F);
    means.bind();
    shaders.means.use();
    bindTexture(gl, 0, copy.texture);
    shaders.means.setSampler('Picture', 0);
    setIVec2(shaders.means, 'Grid', u.Grid[0], u.Grid[1]);
    shaders.means.setInt('OffsetRows', u.OffsetRows);
    shaders.means.set('BoardOrigin', u.BoardOrigin[0], u.BoardOrigin[1]);
    shaders.means.set('Pitch', u.Pitch);
    shaders.means.set('PictureSize', u.PictureSize[0], u.PictureSize[1]);
    shaders.means.set('PictureLod', u.PictureLod);
    quad.draw();
    // The plugin reads GL_RED / GL_FLOAT. WebGL2 guarantees only RGBA / FLOAT
    // from a float target, so all four channels come back and red is kept.
    const n = columns * rows;
    if (readBuffer.length !== n * 4) readBuffer = new Float32Array(n * 4);
    gl.readPixels(0, 0, columns, rows, gl.RGBA, gl.FLOAT, readBuffer);
    const luma = new Float32Array(n);
    for (let i = 0; i < n; i += 1) luma[i] = readBuffer[i * 4];
    telemetry.readBack = true;
    return luma;
  }

  return {
    engine,
    render({ input, params, width, height, time }) {
      //------------------------------------------------------------------
      // The host's parameter writes. Update Now is an event: one press is
      // SetFloatParameter( 1 ), and the page releases the button.
      //------------------------------------------------------------------
      for (const d of ON_PAGE) {
        if (d.type === 'event') continue;
        engine.setParam(d.index, hostValue(d, params.get(d.id)));
      }
      if (params.get('updateNow') > 0.5) {
        engine.setParam(S.PT.UPDATE_NOW, 1);
        params.set('updateNow', 0, { silent: true });
      }

      const pictureWidth = input.width;
      const pictureHeight = input.height;
      copy.ensure(pictureWidth, pictureHeight, gl.RGBA8);
      gl.disable(gl.BLEND);

      //------------------------------------------------------------------
      // 1. The clip, into a texture of ours, with a mip chain. A browser
      // texture is unpadded, so MaxUV is (1, 1).
      //------------------------------------------------------------------
      copy.bind();
      shaders.copy.use();
      bindTexture(gl, 0, input.texture);
      shaders.copy.setSampler('InputTexture', 0);
      shaders.copy.set('MaxUV', 1.0, 1.0);
      quad.draw();
      copy.generateMipmap();

      //------------------------------------------------------------------
      // 2. The means (when a pass needs them), the dither, the driver and
      // the discs: sign.js. The clock is the kit's, in seconds.
      //------------------------------------------------------------------
      telemetry.readBack = false;
      const out = engine.frame(time, pictureWidth, pictureHeight, width, height, readMeans);

      //------------------------------------------------------------------
      // 3. uploadAngles: cos and sin, worked out in double on the CPU.
      //------------------------------------------------------------------
      gl.bindTexture(gl.TEXTURE_2D, turnTexture);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
      if (out.columns !== turnColumns || out.rows !== turnRows) {
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RG32F, out.columns, out.rows, 0, gl.RG, gl.FLOAT, out.turn);
        turnColumns = out.columns;
        turnRows = out.rows;
      } else {
        gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, out.columns, out.rows, gl.RG, gl.FLOAT, out.turn);
      }
      gl.bindTexture(gl.TEXTURE_2D, null);

      //------------------------------------------------------------------
      // 4. The board, straight to the canvas: the host's viewport.
      //------------------------------------------------------------------
      const b = out.board;
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, width, height);
      shaders.board.use();
      bindTexture(gl, 0, copy.texture);
      bindTexture(gl, 1, turnTexture);
      shaders.board.setSampler('Picture', 0);
      shaders.board.setSampler('Turn', 1);
      setIVec2(shaders.board, 'Grid', b.Grid[0], b.Grid[1]);
      shaders.board.setInt('OffsetRows', b.OffsetRows);
      shaders.board.set('BoardOrigin', b.BoardOrigin[0], b.BoardOrigin[1]);
      shaders.board.set('BoardSize', b.BoardSize[0], b.BoardSize[1]);
      shaders.board.set('Pitch', b.Pitch);
      shaders.board.set('OutSize', b.OutSize[0], b.OutSize[1]);
      shaders.board.set('DiscSize', b.DiscSize);
      shaders.board.set('FaceColour', b.FaceColour[0], b.FaceColour[1], b.FaceColour[2]);
      shaders.board.set('LightSinCos', b.LightSinCos[0], b.LightSinCos[1]);
      shaders.board.set('MixAmount', b.MixAmount);
      quad.draw();

      bindTexture(gl, 1, null);
      gl.activeTexture(gl.TEXTURE0);
      if (hooks.afterRender) hooks.afterRender(gl, input);

      telemetry.columns = out.columns;
      telemetry.rows = out.rows;
      telemetry.stuck = engine.sign.stuckCount;
      telemetry.mode = out.mode;
      telemetry.fires = engine.fires;
      telemetry.active = engine.sign.active;
      telemetry.scanRate = out.settings.scanRate;
      telemetry.scanRows = out.settings.scanRows;
      telemetry.flipTime = out.settings.flipTime;
      telemetry.dt = out.dt;
      telemetry.frames += 1;
    },
  };
}

//===========================================================================
// The controls, from sign.js's DECLARED, which check_port.mjs holds to the
// constructor: same names, groups, order, types, defaults and elements.
//===========================================================================

/// FF_TYPE_INTEGER is exempt from the 0..1 clamp, so the plugin stores these
/// as the integer itself. The kit has no integer control, so -- as copperlist,
/// teletext and splitflap did -- they are dropdowns of every value in the
/// plugin's range, and hostValue() turns the index back into the integer.
function integerElements(d) {
  const out = [];
  for (let v = d.range[0]; v <= d.range[1]; v += 1) out.push(String(v));
  return out;
}

const seconds = (s) => (s < 1 ? `${(s * 1000).toPrecision(3)} ms` : `${s.toPrecision(3)} s`);
const degrees = (r) => `${(r * 180 / Math.PI).toFixed(1)}°`;

const HINTS = {
  columns: { hint: 'Discs across the sign, 4 to 192. FF_TYPE_INTEGER in the plugin; the kit has no integer control, so this is a dropdown of every value. A change re-maps the sign: each new disc takes the old disc at the same place, mid-swing included.' },
  rows: { hint: 'Discs down the sign, 2 to 108. FF_TYPE_INTEGER in the plugin, a dropdown here. Regrids like Columns.' },
  layout: { hint: 'Square: a plain grid. Offset: odd rows sit half a pitch to the right, and the sign is half a pitch wider. The pitch is square, as large as fits, centred; a sign that does not fill the frame leaves a transparent surround.' },
  discSize: { display: (v) => `${(S.discSizeFromParam(v) * 100).toFixed(0)} % of the pitch`, hint: 'Disc diameter over the pitch, 40 to 100 %. Each disc sits in a recess a little wider than itself.' },
  colour: { hint: 'The fluorescent face: daylight yellow, green or white. The other face is black.' },
  scan: { hint: 'Which way the driver scans: one column at a time (the wipe runs left to right) or one row at a time (top to bottom).' },
  scanRate: { display: (v) => `${S.scanRateFromParam(v).toPrecision(3)} lines/s`, hint: 'Lines the driver visits a second, 4 to 4,000, geometric. Line L of a pass is visited L / Scan Rate seconds after the pass began, to the double, inside whatever frame that falls in.' },
  update: { hint: 'Continuous: the driver sweeps for ever against the live clip, so the sign lags it by up to a sweep. Interval: an update every Interval seconds, and on the first frame. Onset: when the audio detector fires, which never happens on this page because no audio reaches it. Manual: only on Update Now. An update latches the picture and starts one pass; one during a pass latches the new picture at once and queues one more pass.' },
  interval: { display: (v) => seconds(S.intervalFromParam(v)), hint: 'Seconds between updates in Interval mode, 0.1 to 10, geometric, ticking from the moment the mode was chosen. A stall longer than the interval gives one update, not a burst.' },
  onlyChanges: { hint: 'On: the driver pulses only the discs whose target differs from what it last wrote to them (a driver cannot read a disc back). Off, Refresh All: every disc is pulsed, and one already showing the right face is driven into its own stop and rebounds, which is invisible at Rebound 0.' },
  updateNow: { hint: 'FF_TYPE_EVENT in the plugin: one press, one update, in any Update mode. The kit has no event control, so this is a button.' },
  flipTime: { display: (v) => `${seconds(S.flipTimeFromParam(v))} a swing`, hint: 'Seconds for a disc to first reach the far stop, 5 ms to 1 s, geometric. The swing is a constant torque from rest, θ = π u², so a disc arrives at its fastest; the rebound follows.' },
  rebound: { display: (v) => `e = ${S.reboundFromParam(v).toFixed(2)}`, hint: 'Restitution at the stop, 0 to 0.6. Rebound k leaves at 2π e^k, lasts e^k swings and rises π e^(2k) / 2; the train is cut once a rebound would rise less than half a degree, so a settled sign is still to the bit.' },
  stuck: { display: (v) => `${(S.stuckFromParam(v) * 100).toFixed(2)} % never move`, hint: 'The fraction of discs jammed on a seeded side, 0 to 20 %: exactly round(f × N) of them, the first of a seeded order, so raising it only adds discs. A disc freed by lowering it stays where it was jammed.' },
  late: { display: (v) => `${(S.lateFromParam(v) * 100).toFixed(1)} % on weak coils`, hint: 'The fraction of discs on a weak coil, 0 to 50 %, each swinging 1.5 to 3 times slower (seeded).' },
  dither: { hint: 'One bit a disc from its cell’s mean luma. Threshold: on at or above Threshold. Bayer 4x4: the ordered matrix. Floyd-Steinberg: error diffusion in serpentine order, which is serial, which is why the plugin reads the means back and does this on the CPU.' },
  threshold: { display: (v) => `luma ${Math.fround(v).toFixed(3)} lights half`, hint: 'The luma that lights half the discs: every dither first puts the luma through a curve through (0, 0), (Threshold, ½) and (1, 1), so black stays black and white stays white whatever it is set to.' },
  light: { display: (v) => `${degrees(S.lightAngleFromParam(v))} from the left`, hint: 'The light in the horizontal plane, from the viewer (0°) to 80° from the left of the sign; Lambert with 30 % ambient. The recess shades the far side of each disc from an oblique light, and a disc edge-on catches it.' },
  mix: {},
};

function control(d) {
  const extra = HINTS[d.id] ?? {};
  switch (d.type) {
    case 'integer':
      return { id: d.id, name: d.name, type: 'option', elements: integerElements(d), default: d.default - d.range[0], group: d.group, hint: extra.hint };
    case 'option':
      return { id: d.id, name: d.name, type: 'option', elements: d.elements, default: d.default, group: d.group, hint: extra.hint };
    case 'boolean':
    case 'event':
      return { id: d.id, name: d.name, type: 'boolean', default: d.default, group: d.group, hint: extra.hint };
    default:
      return { id: d.id, name: d.name, type: 'standard', default: d.default, group: d.group, ...extra };
  }
}

const index = (id, value) => {
  const d = S.DECLARED.find((x) => x.id === id);
  return value - d.range[0];
};

// For a headless driver: the telemetry, the render hook, the port's Engine
// and the mounted demo, so a script can pause, redraw and read pixels.
window.__flipdotDemo = { telemetry, hooks, engine: null, demo: null, port: S };

const demo = mountDemo({
  name: 'Flipdot',
  pluginId: 'FD01',
  kind: 'effect',
  tagline:
    'The picture on an electromagnetic flip-dot sign: a grid of bistable discs, black on one side and fluorescent on the other, swung by coil pulses from a driver board that scans one column (or row) at a time and pulses only what changes. A whole-sign change sweeps across at the scan rate, every disc takes its time to swing and rebounds off the stop, and nothing moves without a pulse. The shaders here are the plugin’s own; the sign, the driver, the disc’s profile and the dither are a port of its C++.',
  repo: 'https://github.com/stoatworks-labs/flipdot',
  page: 'https://stoatworks-labs.com/software/flipdot/',

  blurb:
    'It is Flipdot’s own GLSL, ported from the repository to WebGL2 — the means pass read back to the CPU exactly as in the plugin — with the sign, its driver, the disc’s swing and rebound, the dither and the update decision ported to JavaScript and checked against the plugin’s C++ on the repository’s own cases. It runs on generated clips in this page, with the plugin’s own parameters and no install. No audio reaches it, so Onset mode never fires.',

  // The means pass renders into R32F, which WebGL2 does only with
  // EXT_color_buffer_float. Without it the page says so rather than
  // quantising every disc's mean to 8 bits.
  needFloat: true,
  // A sign that does not fill the frame leaves a transparent surround.
  showBackdrop: true,

  params: ON_PAGE.map(control),

  // A moving picture is what the driver chases; the cards show the dither and
  // the regrid. The kit's alpha clip is left out: it is premultiplied, and
  // whether Resolume hands the plugin straight or premultiplied alpha is an
  // open question in the plugin's own notes.
  sources: ['scene', 'spot', 'ramp', 'grid', 'bars', 'detail'],

  // The plugin ships no factory presets. These are the page's own, expressed
  // entirely in the plugin's parameters and reachable with the controls.
  presets: {
    'Slow driver: watch the wipe': { scanRate: S.scanRateToParam(20), flipTime: S.flipTimeToParam(0.12) },
    'Manual: press Update Now': { update: 3, scanRate: S.scanRateToParam(60), flipTime: S.flipTimeToParam(0.15) },
    'Refresh All, full rebound': { onlyChanges: 0, rebound: 1, light: 1, scanRate: S.scanRateToParam(30), flipTime: S.flipTimeToParam(0.2) },
    'Big discs, slow swings, lit from the side': { columns: index('columns', 24), rows: index('rows', 14), flipTime: S.flipTimeToParam(0.5), rebound: S.reboundToParam(0.45), light: 1, scanRate: S.scanRateToParam(12) },
    'Old sign: stuck and late': { stuck: S.stuckToParam(0.05), late: S.lateToParam(0.25) },
    'Green, row by row, Floyd-Steinberg': { colour: 1, scan: 1, dither: 2, scanRate: S.scanRateToParam(60) },
    'Offset rows, white discs, hard threshold': { layout: 1, colour: 2, dither: 0, threshold: 0.35 },
    'Every two seconds': { update: 1, interval: S.intervalToParam(2.0), scanRate: S.scanRateToParam(120) },
  },

  differences: [
    'The CPU half of this plugin is a PORT, not the plugin’s own code. Flipdot keeps the sign on the CPU in double — every disc’s side, motion and age, the stuck and late sets, and the driver’s pass (Sign.cpp) with the disc’s stated swing and rebound (Disc.cpp) — dithers the read-back of the means to one bit a disc (Dither.cpp), converts every control (Controls.cpp), runs an onset detector (Onset.cpp), and sequences each frame in ProcessOpenGL. All of that is ported to JavaScript doubles in sign.js. The repository’s demo/tools/check_port.sh builds the plugin’s own C++ from those files and from text cut out of Flipdot.cpp, and on its cases (799 frames of ProcessOpenGL, the control laws, the profile, the geometry and all 20 declarations) the port agrees with it exactly, float for float. That is evidence about the port on those cases, not a proof, and it was run under Node: your browser’s own Math.pow, Math.cos and Math.sin are its own, and one that rounds differently in the last place could put a disc a float away.',
    'The GPU half is not a port. The four programs — vertex, copy, means and board — are the plugin’s own GLSL, copied by demo/tools/splice_shaders.py, and demo/tools/check_shaders.py fails the repository’s verify script if a character of any of them drifts.',
    'The read-back is the plugin’s: one R32F texel per disc, its cell’s mean luma × alpha from sixteen taps at a whole mip level, brought back to the CPU every frame in Continuous and only on an update otherwise. The plugin reads GL_RED / GL_FLOAT; WebGL2 guarantees only RGBA / FLOAT from a float target, so this page reads four channels and keeps red. The browser’s driver generates the mip chain and its bilinear weights are its own, so a disc’s mean on a real picture can sit a level or two from Resolume’s and flip a disc on a threshold. This page needs EXT_color_buffer_float for the R32F target and says so rather than falling back to 8 bits.',
    'Nothing here is a performance claim. The read-back is a GPU sync stall, in the plugin as on this page; how long it takes inside Resolume has not been measured, and this page measures nothing.',
    'No audio reaches a browser page. The plugin’s Audio FFT buffer (64 bins from Resolume) is absent rather than present and dead; the ported onset detector is handed silence every frame, which it reads exactly as the plugin reads an unrouted input. Onset mode therefore never fires here — the sign holds whatever it shows until Update Now — and nothing on the page fakes an onset.',
    'Update Now is FF_TYPE_EVENT in the plugin. The kit has no event control, so it is a button: one press, which the port counts as the plugin does.',
    'Columns and Rows are FF_TYPE_INTEGER in the plugin. The kit has no integer control, so they are dropdowns of every value in the plugin’s range.',
    'The plugin’s Clock measures whether the host sends seconds or milliseconds before it believes a delta. This page’s clock is the kit’s time with the unit declared as seconds, as the repository’s harness declares it; the vote never runs here. The 0.25 s clamp on a frame delta is ported; the kit also never advances a page frame by more than 0.1 s. Restart sends the clock backwards, which the clamp reads as a delta of zero: the sign is bistable and keeps its picture.',
    'In Resolume a clip retrigger re-initialises the GL side, which re-primes the onset detector and restarts Interval’s tick but, by design, leaves the sign where it was. This page never retriggers, so that path is only in the port and check_port’s reading of it, not on screen.',
    'The plugin reads the clip through the host’s MaxUV, because Resolume pads a texture; a browser texture is unpadded, so MaxUV is (1, 1) here, and the picture is the canvas’s size.',
    'The plugin’s numerical proof — the wipe to the frame, Only Changes against Refresh All, bistability, the stuck count, the disc’s width against an independent integration of the torques, the resize and the regrid, the primed detector — is an offline harness in the repository (fdtest). Nothing on this page measures anything; the line under the canvas reports what the ported driver did.',
    'The About block is absent here, as on every page in this suite.',
  ],

  createRenderer: (gl, quad) => {
    const renderer = createRenderer(gl, quad);
    window.__flipdotDemo.engine = renderer.engine;
    return renderer;
  },
});

//---------------------------------------------------------------------------
// After mount: Update Now is an event, so its row gets a button rather than
// the kit's On/Off toggle; a toggle reading "Off" for a control that cannot
// be on is the wrong picture of a host's inspector.
//---------------------------------------------------------------------------
window.__flipdotDemo.demo = demo;

if (demo && !new URLSearchParams(window.location.search).has('embed')) {
  for (const row of document.querySelectorAll('.prow--boolean')) {
    const label = row.querySelector('.prow__name');
    if (!label || label.textContent !== 'Update Now') continue;
    const toggle = row.querySelector('.prow__toggle');
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'btn';
    button.textContent = 'Update Now';
    button.title = 'One press: the driver latches the clip and starts a pass, in any Update mode.';
    button.addEventListener('click', () => {
      demo.params.set('updateNow', 1);
      demo.redraw();
    });
    if (toggle) toggle.replaceWith(button);
    else row.append(button);
  }

  //-------------------------------------------------------------------------
  // The line under the canvas: the ported driver's own numbers.
  //-------------------------------------------------------------------------
  const stage = document.querySelector('.stage');
  if (stage) {
    const line = document.createElement('p');
    line.className = 'stage__status';
    stage.append(line);
    setInterval(() => {
      const t = telemetry;
      if (!t.columns) return;
      const lines = t.scanRows ? t.rows : t.columns;
      let mode = S.UPDATE_NAMES[t.mode];
      if (t.mode === S.K_UPDATE_ONSET) mode += ' (no audio reaches this page, so it never fires)';
      line.textContent =
        `${t.columns} × ${t.rows} discs, ${t.stuck} stuck; a sweep of ${lines} ${t.scanRows ? 'rows' : 'columns'} takes ${seconds(lines / t.scanRate)}, `
        + `${seconds(t.flipTime)} a swing. Update: ${mode}`
        + (t.mode === S.K_UPDATE_CONTINUOUS ? ', the driver sweeping for ever against the live clip; ' : `; ${t.fires.toLocaleString('en-GB')} update${t.fires === 1 ? '' : 's'} so far; `)
        + `the driver is ${t.active ? 'in a pass' : 'idle'}. Last frame delta ${(t.dt * 1000).toFixed(1)} ms (clamped at 250).`;
    }, 250);
  }
}
