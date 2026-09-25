/**
    fdtest -- render Flipdot offline, and check what its discs are doing.

    Every check here reads the picture the shipped shaders drew, through the
    real plugin class in a headless CGL context, and never the sign's state.
    A disc's side is the colour of its centre pixel; "a disc moved" is its
    cell's pixels changing; a settle is its cell becoming bit-identical to
    the last frame for good; the swing is the width of its colour face on the
    horizontal through its centre, against the harness's OWN integration of
    the stated torques (not the plugin's closed form).

        fdtest --out /tmp/f.png [--size WxH] [--frames N] [--set "Name=v"]...
        fdtest --list | --names
        fdtest --wipe | --changes | --bistable | --stuck | --rotation | --resize | --prime
        fdtest --negative       every check against its broken model, must FAIL
        fdtest --bench
        fdtest --pipe --size WxH [--fps N] [--frames N] [--script cues.txt]

    Every check runs at 640x360 and at 320x180 -- the raster CI uses -- and
    reports each. FDTEST_RENDERER=software asks for Apple's software renderer
    by id, which is what a GPU-less runner falls back to.

    `--pipe` takes the fleet's frame format so one script can film any of the
    FFGL plugins: raw RGBA frames in on stdin, raw RGBA frames out on stdout.
    The cue script is `frame  Parameter Name  value` lines, held before the
    first key and after the last. Standard parameters ramp between keys;
    options, booleans, integers and events STEP, because a ramp through an
    option fires every intermediate one, and an event half-way up a ramp is a
    press nobody keyed. SIGPIPE is ignored: a reader that hangs up makes the
    write fail and the harness exit 1, not 141.
*/

#include "Controls.h"
#include "Flipdot.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace flipdot;

namespace
{
constexpr double kPi = 3.14159265358979323846;

//---------------------------------------------------------------------------
// PNG out. zlib ships with the OS.
//---------------------------------------------------------------------------
void putU32( std::vector< unsigned char >& out, uint32_t value )
{
	for( int shift = 24; shift >= 0; shift -= 8 )
		out.push_back( static_cast< unsigned char >( value >> shift ) );
}

void putChunk( std::vector< unsigned char >& out, const char* type, const std::vector< unsigned char >& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

bool writePng( const std::string& path, int width, int height, const std::vector< unsigned char >& rgba )
{
	std::vector< unsigned char > raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = 0; y < height; ++y )
	{
		raw.push_back( 0 );
		const unsigned char* row = rgba.data() + static_cast< size_t >( y ) * width * 4;
		raw.insert( raw.end(), row, row + static_cast< size_t >( width ) * 4 );
	}
	uLongf size = compressBound( static_cast< uLong >( raw.size() ) );
	std::vector< unsigned char > compressed( size );
	if( compress2( compressed.data(), &size, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( size );

	std::vector< unsigned char > png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	std::vector< unsigned char > ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.insert( ihdr.end(), { 8, 6, 0, 0, 0 } );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );

	FILE* file = std::fopen( path.c_str(), "wb" );
	if( file == nullptr )
		return false;
	const size_t written = std::fwrite( png.data(), 1, png.size(), file );
	std::fclose( file );
	return written == png.size();
}

//---------------------------------------------------------------------------
// GL. FDTEST_RENDERER=software asks for Apple's software renderer by id, on
// a Mac that has a GPU: it is what a GPU-less CI runner falls back to, so a
// check that fails only in CI can be reproduced here (repousse's recipe, by
// way of stencil).
//---------------------------------------------------------------------------
CGLContextObj gContext = nullptr;

bool openGL()
{
	if( gContext != nullptr )
		return true;
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated, kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ), static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute software[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ), static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute generic[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFARendererID, static_cast< CGLPixelFormatAttribute >( kCGLRendererGenericFloatID ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ), static_cast< CGLPixelFormatAttribute >( 0 )
	};
	CGLPixelFormatObj format = nullptr;
	GLint count              = 0;
	const char* renderer     = std::getenv( "FDTEST_RENDERER" );
	if( renderer != nullptr && std::strcmp( renderer, "software" ) == 0 )
	{
		if( CGLChoosePixelFormat( generic, &format, &count ) != kCGLNoError || format == nullptr )
		{
			std::fprintf( stderr, "fdtest: FDTEST_RENDERER=software, but the software renderer is not available\n" );
			return false;
		}
	}
	else if( CGLChoosePixelFormat( accelerated, &format, &count ) != kCGLNoError || format == nullptr )
	{
		if( CGLChoosePixelFormat( software, &format, &count ) != kCGLNoError || format == nullptr )
		{
			std::fprintf( stderr, "fdtest: could not choose a pixel format\n" );
			return false;
		}
	}
	const CGLError error = CGLCreateContext( format, nullptr, &gContext );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
	{
		std::fprintf( stderr, "fdtest: could not create an OpenGL context\n" );
		return false;
	}
	CGLSetCurrentContext( gContext );
	if( renderer != nullptr && std::strcmp( renderer, "software" ) == 0 )
		std::fprintf( stderr, "fdtest: software renderer: %s\n", reinterpret_cast< const char* >( glGetString( GL_RENDERER ) ) );
	return true;
}

struct Image
{
	int width = 0, height = 0;
	std::vector< unsigned char > px;///< RGBA, top row first

	unsigned char at( int x, int y, int c = 0 ) const
	{
		return px[ ( static_cast< size_t >( y ) * width + x ) * 4 + c ];
	}
	bool operator==( const Image& o ) const
	{
		return width == o.width && height == o.height && px == o.px;
	}
};

/// A test card: a soft gradient with a disc, a ring and a bar, so a default
/// render has tones to dither and edges to wipe.
Image card( int width, int height )
{
	Image img;
	img.width  = width;
	img.height = height;
	img.px.resize( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const float u = ( x + 0.5f ) / width, v = ( y + 0.5f ) / height;
			float r = 0.15f + 0.6f * u, g = 0.15f + 0.6f * u, b = 0.15f + 0.6f * u;
			const float aspect = static_cast< float >( width ) / height;
			const float dx1 = ( u - 0.25f ) * aspect, dy1 = v - 0.5f;
			if( std::sqrt( dx1 * dx1 + dy1 * dy1 ) < 0.2f )
				r = g = b = 0.95f;
			const float dx2 = ( u - 0.6f ) * aspect, dy2 = v - 0.5f, d2 = std::sqrt( dx2 * dx2 + dy2 * dy2 );
			if( d2 < 0.22f && d2 > 0.14f )
			{
				r = 0.9f;
				g = 0.5f;
				b = 0.1f;
			}
			if( u > 0.82f && u < 0.94f && v > 0.15f && v < 0.85f )
				r = g = b = 0.02f;
			unsigned char* p = &img.px[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			p[ 0 ]           = static_cast< unsigned char >( r * 255.0f + 0.5f );
			p[ 1 ]           = static_cast< unsigned char >( g * 255.0f + 0.5f );
			p[ 2 ]           = static_cast< unsigned char >( b * 255.0f + 0.5f );
			p[ 3 ]           = 255;
		}
	return img;
}

/// The harness's own statement of where the sign sits (Square layout):
/// square pitch, as large as fits, centred. Pixels from the top-left.
struct Grid
{
	int width, height, columns, rows;
	double pitch() const
	{
		return std::min( static_cast< double >( width ) / columns, static_cast< double >( height ) / rows );
	}
	double originX() const
	{
		return 0.5 * ( width - pitch() * columns );
	}
	double originY() const
	{
		return 0.5 * ( height - pitch() * rows );
	}
	double centreX( int c ) const
	{
		return originX() + ( c + 0.5 ) * pitch();
	}
	double centreY( int r ) const
	{
		return originY() + ( r + 0.5 ) * pitch();
	}
	/// The pixel whose centre is nearest a disc's centre (ties to the right).
	int px( int c ) const
	{
		return static_cast< int >( std::floor( centreX( c ) ) );
	}
	int py( int r ) const
	{
		return static_cast< int >( std::floor( centreY( r ) ) );
	}
	/// The disc's cell, as whole pixels whose centres fall inside it.
	int x0( int c ) const
	{
		return static_cast< int >( std::ceil( originX() + c * pitch() - 0.5 ) );
	}
	int x1( int c ) const
	{
		return static_cast< int >( std::ceil( originX() + ( c + 1 ) * pitch() - 0.5 ) );
	}
	int y0( int r ) const
	{
		return static_cast< int >( std::ceil( originY() + r * pitch() - 0.5 ) );
	}
	int y1( int r ) const
	{
		return static_cast< int >( std::ceil( originY() + ( r + 1 ) * pitch() - 0.5 ) );
	}
};

/// A picture that is white or black per disc cell: what the checks feed the
/// sign, so a disc's mean is exactly 0 or 1 and its target has no doubt in it.
Image bitCard( const Grid& g, const std::function< bool( int, int ) >& on )
{
	Image img;
	img.width  = g.width;
	img.height = g.height;
	img.px.assign( static_cast< size_t >( g.width ) * g.height * 4, 0 );
	for( int r = 0; r < g.rows; ++r )
		for( int c = 0; c < g.columns; ++c )
		{
			const unsigned char v = on( c, r ) ? 255 : 0;
			for( int y = std::max( g.y0( r ), 0 ); y < std::min( g.y1( r ), g.height ); ++y )
				for( int x = std::max( g.x0( c ), 0 ); x < std::min( g.x1( c ), g.width ); ++x )
				{
					unsigned char* p = &img.px[ ( static_cast< size_t >( y ) * g.width + x ) * 4 ];
					p[ 0 ] = p[ 1 ] = p[ 2 ] = v;
				}
		}
	for( size_t i = 3; i < img.px.size(); i += 4 )
		img.px[ i ] = 255;
	return img;
}

Image flatCard( const Grid& g, bool on )
{
	return bitCard( g, [ = ]( int, int ) { return on; } );
}

/// A seeded bit per disc, for pictures with no structure a scan could align to.
bool seededBit( int c, int r, uint32_t salt )
{
	uint32_t x = static_cast< uint32_t >( c ) * 73856093u ^ static_cast< uint32_t >( r ) * 19349663u ^ salt * 83492791u;
	x          = x * 747796405u + 2891336453u;
	x          = ( ( x >> ( ( x >> 28u ) + 4u ) ) ^ x ) * 277803737u;
	return ( ( ( x >> 22u ) ^ x ) & 1u ) != 0;
}

/// The plugin in a headless context: an input texture, an output FBO and a
/// synthetic 60 fps clock. `frame()` renders one frame and reads it back.
struct Session
{
	FlipdotPlugin plugin;
	int width = 0, height = 0;
	GLuint input = 0, output = 0, fbo = 0;
	bool initialised = false;
	double fps       = 60.0;
	int frameIndex   = 0;
	float bins[ kAudioBins ] = {};
	/// >= 0: drive the clock the way Resolume does, in milliseconds from here.
	double clockOffsetMs = -1.0;

	bool init()
	{
		if( clockOffsetMs >= 0.0 )
			plugin.ForceMillisecondsClock();
		else
			plugin.ForceSecondsClock();
		FFGLViewportStruct vp = {};
		vp.width              = 16;
		vp.height             = 16;
		if( plugin.InitGL( &vp ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "fdtest: InitGL failed -- see the diagnostics log for which shader\n" );
			return false;
		}
		initialised = true;
		return true;
	}

	/// What a clip trigger does to an effect: the GL side torn down and set up
	/// again. The plugin object, and so the sign, survives it.
	bool retrigger()
	{
		plugin.DeInitGL();
		initialised = false;
		return init();
	}

	~Session()
	{
		if( initialised )
			plugin.DeInitGL();
		release();
	}

	void release()
	{
		if( fbo )
			glDeleteFramebuffers( 1, &fbo );
		if( output )
			glDeleteTextures( 1, &output );
		if( input )
			glDeleteTextures( 1, &input );
		fbo = output = input = 0;
	}

	/// (Re)size the picture. A resize mid-run is the photofinish trap.
	void resize( int w, int h )
	{
		if( w == width && h == height && input != 0 )
			return;
		release();
		width  = w;
		height = h;
		glGenTextures( 1, &input );
		glBindTexture( GL_TEXTURE_2D, input );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		glGenTextures( 1, &output );
		glBindTexture( GL_TEXTURE_2D, output );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glBindTexture( GL_TEXTURE_2D, 0 );
		glGenFramebuffers( 1, &fbo );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0 );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	}

	/// Upload a picture (top row first, as a file has it).
	void setInput( const Image& img )
	{
		resize( img.width, img.height );
		std::vector< unsigned char > flipped( img.px.size() );
		const size_t stride = static_cast< size_t >( width ) * 4;
		for( int y = 0; y < height; ++y )
			std::memcpy( flipped.data() + static_cast< size_t >( y ) * stride,
			             img.px.data() + static_cast< size_t >( height - 1 - y ) * stride, stride );
		glBindTexture( GL_TEXTURE_2D, input );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	bool set( const std::string& name, float value )
	{
		for( unsigned i = 0; i < plugin.GetNumParams(); ++i )
		{
			const char* n = plugin.GetParamName( i );
			if( n && name == n )
			{
				plugin.SetFloatParameter( i, value );
				return true;
			}
		}
		std::fprintf( stderr, "fdtest: no parameter named %s\n", name.c_str() );
		return false;
	}

	void setAudio( float level )
	{
		for( int i = 0; i < kAudioBins; ++i )
			bins[ i ] = level;
	}

	/// Press Update Now for the next frame (an event: 1 on press, 0 on release).
	void press()
	{
		plugin.SetFloatParameter( PT_UPDATE_NOW, 1.0f );
		plugin.SetFloatParameter( PT_UPDATE_NOW, 0.0f );
	}

	/// Render the next frame of the synthetic clock and read it back.
	Image frame()
	{
		for( int i = 0; i < kAudioBins; ++i )
			plugin.SetParamElementValue( PT_AUDIO, static_cast< unsigned >( i ), bins[ i ] );
		if( clockOffsetMs >= 0.0 )
			plugin.SetTime( clockOffsetMs + static_cast< double >( frameIndex ) * 1000.0 / fps );
		else
			plugin.SetTime( static_cast< double >( frameIndex ) / fps );
		++frameIndex;

		FFGLTextureStruct in = {};
		in.Width = in.HardwareWidth = static_cast< FFUInt32 >( width );
		in.Height = in.HardwareHeight = static_cast< FFUInt32 >( height );
		in.Handle                     = input;
		FFGLTextureStruct* inputs[ 1 ] = { &in };
		ProcessOpenGLStruct process    = {};
		process.numInputTextures       = 1;
		process.inputTextures          = inputs;
		process.HostFBO                = fbo;

		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glViewport( 0, 0, width, height );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear( GL_COLOR_BUFFER_BIT );
		if( plugin.ProcessOpenGL( &process ) != FF_SUCCESS )
			std::fprintf( stderr, "fdtest: ProcessOpenGL failed on frame %d\n", frameIndex - 1 );

		Image img;
		img.width  = width;
		img.height = height;
		std::vector< unsigned char > raw( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, raw.data() );
		img.px.resize( raw.size() );
		const size_t stride = static_cast< size_t >( width ) * 4;
		for( int y = 0; y < height; ++y )
			std::memcpy( img.px.data() + static_cast< size_t >( y ) * stride,
			             raw.data() + static_cast< size_t >( height - 1 - y ) * stride, stride );
		return img;
	}

	Image frames( int n )
	{
		Image last;
		for( int i = 0; i < n; ++i )
			last = frame();
		return last;
	}
};

//---------------------------------------------------------------------------
// The settings every measurement shares: a Square sign of yellow discs 0.9 of
// the pitch, Manual updates, Only Changes, no rebound, nothing stuck or late,
// a hard threshold, lit from the viewer. A check that wants otherwise says so.
//---------------------------------------------------------------------------
float discSizeParam( double size )
{
	return static_cast< float >( ( size - 0.4 ) / 0.6 );
}

void plainSign( Session& s, int columns, int rows )
{
	s.set( "Columns", static_cast< float >( columns ) );
	s.set( "Rows", static_cast< float >( rows ) );
	s.set( "Layout", static_cast< float >( kLayoutSquare ) );
	s.set( "Disc Size", discSizeParam( 0.9 ) );
	s.set( "Colour", static_cast< float >( kColourYellow ) );
	s.set( "Scan", static_cast< float >( kScanColumns ) );
	s.set( "Scan Rate", ScanRateToParam( 4000.0 ) );
	s.set( "Update", static_cast< float >( kUpdateManual ) );
	s.set( "Only Changes", 1.0f );
	s.set( "Flip Time", FlipTimeToParam( 0.05 ) );
	s.set( "Rebound", 0.0f );
	s.set( "Stuck", 0.0f );
	s.set( "Late", 0.0f );
	s.set( "Dither", static_cast< float >( kDitherThreshold ) );
	s.set( "Threshold", 0.5f );
	s.set( "Light", 0.0f );
	s.set( "Mix", 1.0f );
}

/// A disc's side, off its centre pixel. The colour face's red is at least
/// 0.3 x 255 = 76 under any light (the ambient floor); the black face's is
/// at most 0.08 x 255 = 20. 50 separates them with room either side.
constexpr int kSideThreshold = 50;

bool sideAt( const Image& img, const Grid& g, int c, int r )
{
	return img.at( g.px( c ), g.py( r ), 0 ) > kSideThreshold;
}

/// FNV-1a over a disc's cell: two frames that agree here agree bit for bit.
uint64_t cellHash( const Image& img, const Grid& g, int c, int r )
{
	uint64_t h = 1469598103934665603ull;
	for( int y = std::max( g.y0( r ), 0 ); y < std::min( g.y1( r ), img.height ); ++y )
		for( int x = std::max( g.x0( c ), 0 ); x < std::min( g.x1( c ), img.width ); ++x )
			for( int k = 0; k < 4; ++k )
			{
				h ^= img.at( x, y, k );
				h *= 1099511628211ull;
			}
	return h;
}

/// The rasters every check runs at.
struct Raster
{
	int width, height;
};
const Raster kRasters[] = { { 640, 360 }, { 320, 180 } };

int gFailures = 0;

void report( bool ok, const char* format, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
void report( bool ok, const char* format, ... )
{
	char buffer[ 1024 ];
	va_list args;
	va_start( args, format );
	std::vsnprintf( buffer, sizeof( buffer ), format, args );
	va_end( args );
	std::printf( "  %s  %s\n", ok ? "ok  " : "FAIL", buffer );
	if( !ok )
		++gFailures;
}

using Debug = FlipdotPlugin::Debug;

//---------------------------------------------------------------------------
// --wipe: after a whole-sign change, the discs of line L complete their swing
// at L / Scan Rate + Flip Time after the update, to the frame, for every line.
//
// The numbers: 25 lines a second (2.4 frames a line) and a 6.5-frame swing,
// so line L lands 2.4 L + 6.5 frames after the press. Its fractional part is
// one of .5 .9 .3 .7 .1 and never within a tenth of a frame of a whole one,
// so the landing frame is ceil( 2.4 L + 6.5 ) with nothing to round, and the
// float Scan Rate and Flip Time (a few parts in 10^7) cannot move it.
//
// It runs three ways: column by column, row by row, and column by column
// with the clock in milliseconds from 499,000,000 (what Resolume was measured
// sending), where only a delta taken in double survives.
//
// "Completes" is read off the picture: the first frame from which the disc's
// cell is bit-identical to the last frame of the run. With no rebound the
// disc is exactly at the stop from the frame it lands. The frame before,
// it is at most 0.985 of the way (u = 1 - 0.1 / 6.5), 0.096 rad short of the
// stop; lit from 80 degrees off-axis the face's shade changes by
// sin(80) x 0.7 x 0.096 = 0.066, 17 levels -- the disc cannot pass for
// landed a frame early. (Lit from the front the shade would change by
// 1 - cos 0.096, a level, and the check would be measuring the rounding.)
//---------------------------------------------------------------------------
void checkWipe( const Debug& debug )
{
	for( const Raster& raster : kRasters )
		for( int variant = 0; variant < 3; ++variant )
		{
			//0 column by column, 1 row by row, 2 column by column on Resolume's
			//clock: milliseconds, from 499,000,000, where a float resolves
			//32 ms and so could not tell one frame from the next.
			const int scanRows = variant == 1 ? 1 : 0;
			constexpr int columns = 16, rows = 9;
			constexpr double rate = 25.0, flipFrames = 6.5;
			const Grid g { raster.width, raster.height, columns, rows };
			Session s;
			if( variant == 2 )
				s.clockOffsetMs = 499.0e6;
			s.plugin.SetDebugForTest( debug );
			if( !s.init() )
				return;
			plainSign( s, columns, rows );
			s.set( "Scan", static_cast< float >( scanRows ? kScanRows : kScanColumns ) );
			s.set( "Scan Rate", ScanRateToParam( rate ) );
			s.set( "Flip Time", FlipTimeToParam( flipFrames / 60.0 ) );
			s.set( "Light", 1.0f );

			s.setInput( flatCard( g, false ) );
			s.frames( 10 );
			s.setInput( flatCard( g, true ) );
			s.press();
			const int lines = scanRows ? rows : columns;
			const int run   = static_cast< int >( std::ceil( ( lines - 1 ) * 60.0 / rate + flipFrames ) ) + 12;
			std::vector< std::vector< uint64_t > > hashes;
			Image last;
			for( int f = 0; f < run; ++f )
			{
				last = s.frame();
				std::vector< uint64_t > h( static_cast< size_t >( columns * rows ) );
				for( int r = 0; r < rows; ++r )
					for( int c = 0; c < columns; ++c )
						h[ static_cast< size_t >( r * columns + c ) ] = cellHash( last, g, c, r );
				hashes.push_back( h );
			}

			int wrong = 0, notYellow = 0, shown = 0;
			for( int r = 0; r < rows; ++r )
				for( int c = 0; c < columns; ++c )
				{
					const size_t i = static_cast< size_t >( r * columns + c );
					int settled    = 0;
					for( int f = 0; f < run; ++f )
						if( hashes[ static_cast< size_t >( f ) ][ i ] != hashes.back()[ i ] )
							settled = f + 1;
					const int line = scanRows ? r : c;
					const int want = static_cast< int >( std::ceil( line * 60.0 / rate + flipFrames ) );
					if( settled != want )
					{
						++wrong;
						if( shown++ < 3 )
							std::printf( "        disc (%d,%d), line %d: completed on frame %d after the press, want %d\n", c, r, line, settled, want );
					}
					if( !sideAt( last, g, c, r ) )
						++notYellow;
				}
			report( wrong == 0 && notYellow == 0,
			        "%dx%d, %s: every disc of line L completed on frame ceil(2.4 L + 6.5) exactly (%d of %d wrong), all yellow (%d not)",
			        raster.width, raster.height, variant == 2 ? "column by column, clock at 499,000,000 ms" : scanRows ? "row by row" : "column by column",
			        wrong, columns * rows, notYellow );
		}
}

//---------------------------------------------------------------------------
// --changes: Only Changes pulses exactly the discs whose target differs;
// Refresh All pulses every disc, and each still ends on its target.
//
// "Moved" is a disc's cell differing, on any frame of the update, from the
// settled frame before it. A disc pulsed toward the face it already shows
// is driven into its stop and rebounds (Rebound 0.6: 0.57 rad off the stop,
// then back to it exactly), so under Refresh All an unchanged disc moves and
// then returns bit-identical. Lit 80 degrees off-axis: the colour face
// brightens 60 levels at that rebound and the black face's edge moves
// 1 - cos 0.57 = 16 % of its radius, a pixel and a half at 320x180.
//---------------------------------------------------------------------------
void checkChanges( const Debug& debug )
{
	for( const Raster& raster : kRasters )
		for( int refresh = 0; refresh < 2; ++refresh )
		{
			constexpr int columns = 16, rows = 9;
			const Grid g { raster.width, raster.height, columns, rows };
			Session s;
			s.plugin.SetDebugForTest( debug );
			if( !s.init() )
				return;
			plainSign( s, columns, rows );
			s.set( "Scan Rate", ScanRateToParam( 400.0 ) );
			s.set( "Flip Time", FlipTimeToParam( 0.1 ) );
			s.set( "Rebound", ReboundToParam( 0.6 ) );
			s.set( "Light", 1.0f );

			auto a = []( int c, int r ) { return seededBit( c, r, 1 ); };
			auto b = []( int c, int r ) { return seededBit( c, r, 2 ); };
			s.setInput( bitCard( g, a ) );
			s.frames( 2 );
			s.press();
			const Image before = s.frames( 60 );

			s.set( "Only Changes", refresh ? 0.0f : 1.0f );
			s.setInput( bitCard( g, b ) );
			s.press();
			std::vector< bool > moved( static_cast< size_t >( columns * rows ), false );
			Image last;
			for( int f = 0; f < 60; ++f )
			{
				last = s.frame();
				for( int r = 0; r < rows; ++r )
					for( int c = 0; c < columns; ++c )
						if( cellHash( last, g, c, r ) != cellHash( before, g, c, r ) )
							moved[ static_cast< size_t >( r * columns + c ) ] = true;
			}

			int differ = 0, movedCount = 0, wrongSet = 0, wrongSide = 0, notBack = 0;
			for( int r = 0; r < rows; ++r )
				for( int c = 0; c < columns; ++c )
				{
					const size_t i  = static_cast< size_t >( r * columns + c );
					const bool diff = a( c, r ) != b( c, r );
					differ += diff ? 1 : 0;
					movedCount += moved[ i ] ? 1 : 0;
					if( moved[ i ] != ( refresh ? true : diff ) )
						++wrongSet;
					if( sideAt( last, g, c, r ) != b( c, r ) )
						++wrongSide;
					if( !diff && cellHash( last, g, c, r ) != cellHash( before, g, c, r ) )
						++notBack;
				}
			if( refresh )
				report( wrongSet == 0 && wrongSide == 0 && notBack == 0,
				        "%dx%d, Refresh All: %d of %d discs moved (every one pulsed; %d targets differed), %d on the wrong side, %d unchanged discs not back bit-identical",
				        raster.width, raster.height, movedCount, columns * rows, differ, wrongSide, notBack );
			else
				report( wrongSet == 0 && wrongSide == 0 && notBack == 0,
				        "%dx%d, Only Changes: %d discs moved, %d targets differed, %d not in the differing set, %d on the wrong side",
				        raster.width, raster.height, movedCount, differ, wrongSet, wrongSide );
		}
}

//---------------------------------------------------------------------------
// --bistable: with no update the sign holds, bit for bit.
//
//   (a) Manual, settled on the test card: 90 frames with the clip frozen.
//   (b) the clip changes and nothing updates: still the same picture.
//   (c) Continuous, the driver sweeping a frozen clip four times a second,
//       Only Changes: nothing to pulse, 90 identical frames.
//
// The defaults otherwise (Bayer, the default rebound, a few stuck and late
// discs), because the claim is about the sign as shipped. The bound on
// settling: a 64-column sweep at 240 lines/s is 0.27 s, a weak disc's swing
// at most 3 x 40 ms plus its rebound train (0.43 flip units at e = 0.3): 0.4 s
// at most; 60 frames is 1 s.
//---------------------------------------------------------------------------
void checkBistable( const Debug& debug )
{
	for( const Raster& raster : kRasters )
	{
		{
			Session s;
			s.plugin.SetDebugForTest( debug );
			if( !s.init() )
				return;
			s.set( "Update", static_cast< float >( kUpdateManual ) );
			s.setInput( card( raster.width, raster.height ) );
			s.frames( 2 );
			s.press();
			const Image settled = s.frames( 60 );
			int yellow          = 0;
			for( size_t i = 0; i < settled.px.size(); i += 4 )
				yellow += settled.px[ i ] > kSideThreshold ? 1 : 0;
			int moved = 0;
			for( int f = 0; f < 90; ++f )
				if( !( s.frame() == settled ) )
					++moved;
			report( moved == 0 && yellow > 0, "%dx%d, Manual: the settled test card held for 90 frames (%d differed; %d lit pixels)", raster.width, raster.height, moved, yellow );

			Image other = card( raster.width, raster.height );
			for( size_t i = 0; i < other.px.size(); i += 4 )
				for( int k = 0; k < 3; ++k )
					other.px[ i + static_cast< size_t >( k ) ] = static_cast< unsigned char >( 255 - other.px[ i + static_cast< size_t >( k ) ] );
			s.setInput( other );
			moved = 0;
			for( int f = 0; f < 60; ++f )
				if( !( s.frame() == settled ) )
					++moved;
			report( moved == 0, "%dx%d, Manual: the clip inverted and no update: 60 frames still the old picture (%d differed)", raster.width, raster.height, moved );
		}
		{
			Session s;
			s.plugin.SetDebugForTest( debug );
			if( !s.init() )
				return;
			s.set( "Update", static_cast< float >( kUpdateContinuous ) );
			s.setInput( card( raster.width, raster.height ) );
			const Image settled = s.frames( 60 );
			int moved           = 0;
			for( int f = 0; f < 90; ++f )
				if( !( s.frame() == settled ) )
					++moved;
			report( moved == 0, "%dx%d, Continuous, Only Changes: the driver sweeping a frozen clip, 90 frames identical (%d differed)", raster.width, raster.height, moved );
		}
	}
}

//---------------------------------------------------------------------------
// --stuck: the stuck discs never move, and there are exactly round( f x N ).
//
// Counted off the picture: the discs whose cell never changed while the sign
// was driven white, black and white again. The plugin seeds the set as the
// first round( f x N ) of a seeded order of all N discs, so the count is the
// stated fraction to the disc, and the check also asks that the set seen is
// the set the plugin seeded, and that a larger fraction keeps the smaller
// one's discs. With no stuck discs the check could not fail by counting, so
// the count is required to be the stated one, not "some".
//---------------------------------------------------------------------------
void checkStuck( const Debug& debug )
{
	for( const Raster& raster : kRasters )
	{
		constexpr int columns = 32, rows = 18;
		const Grid g { raster.width, raster.height, columns, rows };
		const double fractions[] = { 0.1, 0.2 };
		std::vector< bool > previous;
		for( const double fraction : fractions )
		{
			Session s;
			s.plugin.SetDebugForTest( debug );
			if( !s.init() )
				return;
			plainSign( s, columns, rows );
			s.set( "Stuck", StuckToParam( fraction ) );
			s.setInput( flatCard( g, false ) );
			const Image first = s.frame();
			std::vector< bool > changed( static_cast< size_t >( columns * rows ), false );
			Image last;
			for( int phase = 0; phase < 3; ++phase )
			{
				s.setInput( flatCard( g, phase != 1 ) );
				s.press();
				for( int f = 0; f < 20; ++f )
				{
					last = s.frame();
					for( int r = 0; r < rows; ++r )
						for( int c = 0; c < columns; ++c )
							if( cellHash( last, g, c, r ) != cellHash( first, g, c, r ) )
								changed[ static_cast< size_t >( r * columns + c ) ] = true;
				}
			}
			int never = 0, mismatch = 0, stuckYellow = 0, notWhite = 0;
			std::vector< bool > stuckSeen( static_cast< size_t >( columns * rows ), false );
			for( int r = 0; r < rows; ++r )
				for( int c = 0; c < columns; ++c )
				{
					const size_t i     = static_cast< size_t >( r * columns + c );
					const bool isStuck = !changed[ i ];
					stuckSeen[ i ]     = isStuck;
					never += isStuck ? 1 : 0;
					if( isStuck != s.plugin.SignForTest().TraitsAt( c, r ).stuck )
						++mismatch;
					if( isStuck && sideAt( last, g, c, r ) )
						++stuckYellow;
					if( !isStuck && !sideAt( last, g, c, r ) )
						++notWhite;
				}
			const int want = static_cast< int >( std::llround( fraction * columns * rows ) );
			report( never == want && mismatch == 0 && notWhite == 0,
			        "%dx%d, Stuck %.2f: %d discs never moved (round(%.2f x %d) = %d), %d stuck on the colour face, %d not the seeded set, %d free discs off target",
			        raster.width, raster.height, fraction, never, fraction, columns * rows, want, stuckYellow, mismatch, notWhite );
			if( !previous.empty() )
			{
				int lost = 0;
				for( size_t i = 0; i < previous.size(); ++i )
					if( previous[ i ] && !stuckSeen[ i ] )
						++lost;
				report( lost == 0, "%dx%d: every disc stuck at 0.10 is stuck at 0.20 too (%d lost)", raster.width, raster.height, lost );
			}
			previous = stuckSeen;
		}
	}
}

//---------------------------------------------------------------------------
// The harness's own solution of the swing, independent of Disc.cpp.
//
// Velocity Verlet at 1e-5 flip units on the stated torques: theta'' = 2 pi
// from rest until the far stop; there the velocity reverses times e and the
// latch pulls back at 4 pi; each return to the stop is another impact; once
// a rebound would rise less than half a degree (v^2 / 8 pi < pi / 360) the
// disc stays on the stop. Crossings of the stop are found by interpolating
// inside the step. Disc.cpp states the same model in closed form; this is a
// different computation of it, so a closed form typed wrong disagrees here.
//---------------------------------------------------------------------------
struct HarnessSwing
{
	static constexpr double kStep = 1.0e-5;
	std::vector< double > theta;///< per step, from the pulse

	HarnessSwing( double e, double span )
	{
		const size_t n = static_cast< size_t >( span / kStep ) + 2;
		theta.reserve( n );
		double x = 0.0, v = 0.0;
		bool driven = true, resting = false;
		theta.push_back( 0.0 );
		for( size_t i = 1; i < n; ++i )
		{
			if( resting )
			{
				theta.push_back( kPi );
				continue;
			}
			const double a  = driven ? 2.0 * kPi : 4.0 * kPi;//toward the far stop, either way
			double xn       = x + v * kStep + 0.5 * a * kStep * kStep;
			double vn       = v + a * kStep;
			if( xn >= kPi )
			{
				//The impact inside this step: solve x + v t + a t^2 / 2 = pi.
				const double disc = std::sqrt( std::max( v * v + 2.0 * a * ( kPi - x ), 0.0 ) );
				const double t    = ( -v + disc ) / a;
				const double vi   = v + a * t;//arrival speed
				const double out  = -e * vi;
				driven            = false;
				if( out * out / ( 8.0 * kPi ) < kPi / 360.0 )
				{
					resting = true;
					xn      = kPi;
					vn      = 0.0;
				}
				else
				{
					const double rest = kStep - t;
					xn                = kPi + out * rest + 0.5 * 4.0 * kPi * rest * rest;
					vn                = out + 4.0 * kPi * rest;
				}
			}
			x = xn;
			v = vn;
			theta.push_back( x );
		}
	}

	double at( double u ) const
	{
		if( u <= 0.0 )
			return 0.0;
		const double f = u / kStep;
		const size_t i = std::min( static_cast< size_t >( f ), theta.size() - 2 );
		const double w = f - static_cast< double >( i );
		return theta[ i ] + ( theta[ i + 1 ] - theta[ i ] ) * w;
	}
};

//---------------------------------------------------------------------------
// --rotation: the colour face's width, frame by frame through a swing and
// its rebounds, against 2 r |cos theta(t)|.
//
// The smallest sign the controls allow (4 x 2), discs 0.9 of the pitch
// (r = 72 px at 640x360, 36 at 320x180), a one-second swing (60 frames),
// Rebound 0.5 (the first rebound 0.39 rad, 11 px of width at r = 72), lit
// from the viewer so the face has no lit side. Disc (0,0) is on line 0,
// which the driver visits at the instant of the press, so frame k after
// the press is u = k / 60 exactly.
//
// Two swings: black to colour (the colour face shows from theta = pi/2, and
// through the rebounds), then colour to black (from the stop until pi/2). A
// pixel on the centre row counts as colour face at or above half way between
// what lies under the face's edge and the face's own level at the disc's
// centre. The shader computes coverage from the exact horizontal distance to
// the edge, so the half-way point IS the edge, and the count is the pixel
// centres within +-w: 2w - 1 <= count <= 2w + 1, the pixel-centre rule at
// two edges. What lies under the edge is the rim mid-swing (0.12 x 0.3 =
// 9.2 of 255 lit from the front) or the recess at a stop (0.012 = 3.1); the
// threshold takes their midpoint, 6.1, so either moves the half-way point by
// at most 3.1 / (2 x (76 - 6)) = 0.022 px an edge (76 is the dimmest the face
// gets, edge-on). The 8-bit rounding of the edge pixel and of the centre
// pixel adds 0.5 / 70 = 0.007 px each, an edge. So |count - 2w| <= 1 + 2 x
// ( 0.022 + 0.014 ) = 1.07: the tolerance is 1.1 px, the same at both rasters
// because it is a statement about two edges, not about r.
//---------------------------------------------------------------------------
void checkRotation( const Debug& debug )
{
	for( const Raster& raster : kRasters )
	{
		constexpr int columns = 4, rows = 2;
		constexpr double e = 0.5;
		const Grid g { raster.width, raster.height, columns, rows };
		const double radius = 0.5 * 0.9 * g.pitch();
		const HarnessSwing swing( e, 4.0 );

		Session s;
		s.plugin.SetDebugForTest( debug );
		if( !s.init() )
			return;
		plainSign( s, columns, rows );
		s.set( "Flip Time", FlipTimeToParam( 1.0 ) );
		s.set( "Rebound", ReboundToParam( e ) );

		const int y  = g.py( 0 );
		auto measure = [ & ]( const Image& img ) {
			const int centre = img.at( g.px( 0 ), y, 0 );
			//Midway between the rim (9.2) and the recess (3.1): see above.
			constexpr double kUnderEdge = 6.1;
			int count                   = 0;
			if( centre >= kSideThreshold )
			{
				const double half = 0.5 * ( centre + kUnderEdge );
				for( int x = g.x0( 0 ); x < g.x1( 0 ); ++x )
					count += img.at( x, y, 0 ) >= half ? 1 : 0;
			}
			else
				for( int x = g.x0( 0 ); x < g.x1( 0 ); ++x )
					count += img.at( x, y, 0 ) >= kSideThreshold ? 1 : 0;
			return count;
		};

		s.setInput( flatCard( g, false ) );
		s.frames( 3 );
		double worst[ 2 ] = { 0.0, 0.0 };
		int worstFrame[ 2 ] = { -1, -1 };
		int bad[ 2 ]      = { 0, 0 };
		for( int swingIndex = 0; swingIndex < 2; ++swingIndex )
		{
			const bool toColour = swingIndex == 0;
			s.setInput( flatCard( g, toColour ) );
			s.press();
			for( int k = 0; k < 150; ++k )
			{
				const Image img    = s.frame();
				const double theta = swing.at( k / 60.0 );
				//The colour face shows while its normal points at the viewer.
				const double phi  = toColour ? theta : kPi - theta;
				const double want = std::cos( phi ) < 0.0 ? 2.0 * radius * std::fabs( std::cos( phi ) ) : 0.0;
				const double err  = std::fabs( measure( img ) - want );
				if( err > worst[ swingIndex ] )
				{
					worst[ swingIndex ]      = err;
					worstFrame[ swingIndex ] = k;
				}
				if( std::getenv( "FDTEST_DEBUG_ROTATION" ) && err > 1.0 )
				{
					std::printf( "        debug frame %d want %.3f count %d:", k, want, measure( img ) );
					for( int x = g.x0( 0 ); x < g.x1( 0 ); ++x )
						std::printf( " %d", img.at( x, y, 0 ) );
					std::printf( "\n" );
				}
				if( err > 1.1 )
				{
					if( bad[ swingIndex ]++ < 3 )
						std::printf( "        %s, frame %d (u = %.3f): colour face %d px wide, want %.2f\n", toColour ? "to colour" : "to black", k, k / 60.0, measure( img ), want );
				}
			}
		}
		report( bad[ 0 ] == 0, "%dx%d: black to colour, 150 frames: colour-face width within 1.1 px of 2r|cos theta| (r = %.0f px; worst %.2f px, frame %d)", raster.width, raster.height, radius, worst[ 0 ], worstFrame[ 0 ] );
		report( bad[ 1 ] == 0, "%dx%d: colour to black, 150 frames: within 1.1 px (worst %.2f px, frame %d)", raster.width, raster.height, worst[ 1 ], worstFrame[ 1 ] );
	}
}

//---------------------------------------------------------------------------
// --resize: a picture resize mid-pass keeps every disc's side, and the pass
// carries on; a regrid gives each new disc its parent's side.
//
// (a) 16 x 9, a pass at 10 columns a second (1.6 s) toward a seeded picture;
//     on frame 50 of it (8 columns done) the picture goes 640x360 <-> 320x180.
//     Every disc shows the same side on the frames either side of the resize,
//     and the pass finishes on the target.
// (b) 16 -> 32 columns on a settled sign: disc (c, r) of the new sign shows
//     the side of (c / 2, r) of the old.
//---------------------------------------------------------------------------
void checkResize( const Debug& debug )
{
	for( const Raster& raster : kRasters )
	{
		const Raster other = raster.width == 640 ? Raster { 320, 180 } : Raster { 640, 360 };
		{
			constexpr int columns = 16, rows = 9;
			const Grid g { raster.width, raster.height, columns, rows };
			const Grid h { other.width, other.height, columns, rows };
			auto target = []( int c, int r ) { return seededBit( c, r, 7 ); };
			Session s;
			s.plugin.SetDebugForTest( debug );
			if( !s.init() )
				return;
			plainSign( s, columns, rows );
			s.set( "Scan Rate", ScanRateToParam( 10.0 ) );
			s.setInput( flatCard( g, false ) );
			s.frames( 2 );
			s.setInput( bitCard( g, target ) );
			s.press();
			const Image before = s.frames( 50 );
			s.setInput( bitCard( h, target ) );
			const Image after = s.frame();
			int changed = 0, done = 0;
			for( int r = 0; r < rows; ++r )
				for( int c = 0; c < columns; ++c )
				{
					if( sideAt( before, g, c, r ) != sideAt( after, h, c, r ) )
						++changed;
					done += sideAt( before, g, c, r ) && target( c, r ) ? 1 : 0;
				}
			const Image last = s.frames( 120 );
			int wrong        = 0;
			for( int r = 0; r < rows; ++r )
				for( int c = 0; c < columns; ++c )
					if( sideAt( last, h, c, r ) != target( c, r ) )
						++wrong;
			report( changed == 0 && done > 0 && wrong == 0,
			        "%dx%d -> %dx%d mid-pass: %d of %d discs changed side across the resize (%d already swung), %d off target when the pass ended",
			        raster.width, raster.height, other.width, other.height, changed, columns * rows, done, wrong );
		}
		{
			const Grid g { raster.width, raster.height, 16, 9 };
			const Grid h { raster.width, raster.height, 32, 9 };
			auto target = []( int c, int r ) { return seededBit( c, r, 9 ); };
			Session s;
			s.plugin.SetDebugForTest( debug );
			if( !s.init() )
				return;
			plainSign( s, 16, 9 );
			s.setInput( bitCard( g, target ) );
			s.frames( 2 );
			s.press();
			s.frames( 30 );
			s.set( "Columns", 32.0f );
			const Image img = s.frame();
			int wrong       = 0;
			for( int r = 0; r < 9; ++r )
				for( int c = 0; c < 32; ++c )
					if( sideAt( img, h, c, r ) != target( c / 2, r ) )
						++wrong;
			report( wrong == 0, "%dx%d: 16 -> 32 columns: every new disc shows its parent's side (%d of 288 wrong)", raster.width, raster.height, wrong );
		}
	}
}

//---------------------------------------------------------------------------
// --prime: in Onset mode, audio already loud on the first frame after a clip
// trigger fires no update; a real onset later does; and a retrigger into
// loud audio fires nothing either, with the sign holding its picture.
//
// All 64 bins at 0.5 from frame 0, the clip white (a sign of black discs has
// every disc to change). A fire on frame 0 counts: it is the trap itself.
// Then the bins step to 1.0: flux 64 (1 - sqrt 0.5) = 18.7 against a floor
// seeded at 64 x 0.707 / 8 = 5.7, times 2.5 = 14.1. Nothing here assumes
// the bins are linear in frequency, or anything else about them: all 64 get
// the same value.
//---------------------------------------------------------------------------
void checkPrime( const Debug& debug )
{
	for( const Raster& raster : kRasters )
	{
		constexpr int columns = 16, rows = 9;
		const Grid g { raster.width, raster.height, columns, rows };
		Session s;
		s.plugin.SetDebugForTest( debug );
		if( !s.init() )
			return;
		plainSign( s, columns, rows );
		s.set( "Update", static_cast< float >( kUpdateOnset ) );
		auto anyYellow = [ & ]( const Image& img ) {
			int n = 0;
			for( int r = 0; r < rows; ++r )
				for( int c = 0; c < columns; ++c )
					n += sideAt( img, g, c, r ) ? 1 : 0;
			return n;
		};

		s.setAudio( 0.5f );
		s.setInput( flatCard( g, true ) );
		int fired = 0, moved = 0;
		for( int f = 0; f <= 60; ++f )
		{
			const Image img = s.frame();
			fired += s.plugin.OnsetFiredForTest() ? 1 : 0;
			moved += anyYellow( img ) > 0 ? 1 : 0;
		}
		report( fired == 0 && moved == 0, "%dx%d: steady loud audio from frame 0: no onset, no disc moved in 61 frames (fired %d, moved on %d)", raster.width, raster.height, fired, moved );

		s.setAudio( 1.0f );
		int firedAt = -1;
		Image img;
		for( int f = 0; f < 30; ++f )
		{
			img = s.frame();
			if( firedAt < 0 && s.plugin.OnsetFiredForTest() )
				firedAt = f;
		}
		report( firedAt == 0 && anyYellow( img ) == columns * rows, "%dx%d: a real onset fired on its frame (%d) and the whole sign turned (%d of %d)", raster.width, raster.height, firedAt, anyYellow( img ), columns * rows );

		//A clip trigger into loud audio: the sign is bistable, and the
		//detector primes again.
		s.setAudio( 0.8f );
		s.frames( 10 );
		if( !s.retrigger() )
			return;
		s.setInput( flatCard( g, false ) );
		fired = 0;
		int dark = 0;
		for( int f = 0; f <= 60; ++f )
		{
			img = s.frame();
			fired += s.plugin.OnsetFiredForTest() ? 1 : 0;
			dark += anyYellow( img ) < columns * rows ? 1 : 0;
		}
		report( fired == 0 && dark == 0, "%dx%d: retriggered into loud audio: no onset in 61 frames, and the sign held its picture (fired %d, changed on %d)", raster.width, raster.height, fired, dark );
	}
}

//---------------------------------------------------------------------------
// --negative: every check against a model with one thing broken, and it has
// to notice.
//---------------------------------------------------------------------------
int runNegative()
{
	struct Case
	{
		const char* name;
		Debug debug;
		void ( *check )( const Debug& );
	};
	Debug noScan;
	noScan.sign.noScan = true;
	Debug pulseAll;
	pulseAll.sign.pulseAll = true;
	Debug ignoreStuck;
	ignoreStuck.sign.ignoreStuck = true;
	Debug linear;
	linear.sign.linearProfile = true;
	Debug slow;
	slow.sign.flipScale = 1.15;
	Debug clear;
	clear.clearOnResize = true;
	Debug regrid;
	regrid.sign.clearOnRegrid = true;
	Debug unprimed;
	unprimed.noPrime = true;
	Debug floatClock;
	floatClock.floatClock = true;

	const Case cases[] = {
		{ "--wipe with the whole sign pulsed at once (no scan)", noScan, checkWipe },
		{ "--wipe with the frame delta taken between floats of the host clock", floatClock, checkWipe },
		{ "--changes with every disc pulsed in Only Changes", pulseAll, checkChanges },
		{ "--bistable with every disc pulsed in Only Changes", pulseAll, checkBistable },
		{ "--stuck with stuck discs obeying their coils", ignoreStuck, checkStuck },
		{ "--rotation at constant angular speed", linear, checkRotation },
		{ "--rotation with the swing 15% slower than Flip Time", slow, checkRotation },
		{ "--resize with the sign cleared on a picture resize", clear, checkResize },
		{ "--resize with the sign cleared on a regrid", regrid, checkResize },
		{ "--prime with the detector unprimed", unprimed, checkPrime },
	};

	int missed = 0;
	for( const Case& c : cases )
	{
		std::printf( "negative: %s\n", c.name );
		const int before = gFailures;
		c.check( c.debug );
		const int caught = gFailures - before;
		gFailures        = before;
		std::printf( "  %s  %d assertion(s) failed, as they must\n\n", caught > 0 ? "ok  " : "MISS", caught );
		if( caught == 0 )
			++missed;
	}
	const int total = static_cast< int >( sizeof( cases ) / sizeof( cases[ 0 ] ) );
	std::printf( "negative controls: %d of %d caught\n", total - missed, total );
	return missed == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --list, --names
//---------------------------------------------------------------------------
int runList()
{
	FlipdotPlugin plugin;
	std::printf( "%-4s %-22s %-9s %10s   %-16s\n", "id", "name", "kind", "value", "range" );
	for( unsigned id = 0; id < plugin.GetNumParams(); ++id )
	{
		const char* name = plugin.GetParamName( id );
		if( id >= PT_ABOUT_TEXT )
		{
			std::printf( "%-4u %-22s %-9s %10s   %-16s\n", id, name ? name : "", "about", "-", "-" );
			continue;
		}
		const unsigned type = plugin.GetParamType( id );
		if( type == FF_TYPE_BUFFER )
		{
			std::printf( "%-4u %-22s %-9s %10s   %-16s\n", id, name ? name : "", "buffer", "-", "-" );
			continue;
		}
		const char* kind = "standard";
		char rangeText[ 32 ];
		float lo = 0.0f, hi = 1.0f;
		switch( type )
		{
		case FF_TYPE_BOOLEAN: kind = "boolean"; break;
		case FF_TYPE_EVENT: kind = "event"; break;
		case FF_TYPE_INTEGER:
		{
			kind                = "integer";
			const RangeStruct r = plugin.GetParamRange( id );
			lo                  = r.min;
			hi                  = r.max;
			break;
		}
		case FF_TYPE_OPTION:
			//An option's SDK range reads back 0..1 whatever its element count;
			//the real range is the index of its last element.
			kind = "option";
			hi   = static_cast< float >( plugin.GetNumParamElements( id ) - 1 );
			break;
		default: break;
		}
		std::snprintf( rangeText, sizeof( rangeText ), "[ %g .. %g ]", lo, hi );
		std::printf( "%-4u %-22s %-9s %10.4f   %-16s\n", id, name ? name : "", kind, plugin.GetFloatParameter( id ), rangeText );
	}
	return 0;
}

int runNames()
{
	FlipdotPlugin plugin;
	int bad = 0;
	std::vector< std::string > seen;
	std::printf( "names longer than FFGL's 16 characters, and duplicates:\n\n" );
	for( unsigned id = 0; id < plugin.GetNumParams(); ++id )
	{
		const char* name = plugin.GetParamName( id );
		if( !name )
			continue;
		if( std::strlen( name ) > 16 )
		{
			std::printf( "  %-3u  %-28s %zu characters\n", id, name, std::strlen( name ) );
			++bad;
		}
		if( std::find( seen.begin(), seen.end(), name ) != seen.end() )
		{
			std::printf( "  %-3u  %-28s is a duplicate\n", id, name );
			++bad;
		}
		seen.push_back( name );
		for( unsigned e = 0; e < plugin.GetNumParamElements( id ); ++e )
		{
			const char* el = plugin.GetParamElementName( id, e );
			if( el && std::strlen( el ) > 16 )
			{
				std::printf( "  %-3u  %-28s element %u: %s\n", id, name, e, el );
				++bad;
			}
		}
	}
	std::printf( "\n  %zu names, %d problem(s)\n", seen.size(), bad );
	return bad == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --bench
//---------------------------------------------------------------------------
int runBench()
{
	if( !openGL() )
		return 1;
	struct Size
	{
		const char* name;
		int width, height;
	};
	const Size sizes[] = { { "1280x720 ", 1280, 720 }, { "1920x1080", 1920, 1080 }, { "3840x2160", 3840, 2160 } };
	std::printf( "60 frames each after a 20-frame warm-up, glFinish both sides, the largest sign the controls allow (%d x %d), "
	             "the defaults otherwise (Continuous: a read-back and a dither every frame), the test card and its red inverted alternating every frame, so the driver always has discs to pulse.\n\n",
	             kColumnsMax, kRowsMax );
	std::printf( "resolution   ms/frame   %% of a 60 fps frame\n" );
	for( const Size& size : sizes )
	{
		Session s;
		if( !s.init() )
			return 1;
		s.set( "Columns", static_cast< float >( kColumnsMax ) );
		s.set( "Rows", static_cast< float >( kRowsMax ) );
		//Two pictures uploaded once and swapped by handle: a host hands over a
		//texture it already has, so the bench must not time an upload.
		Image a = card( size.width, size.height );
		Image b = a;
		for( size_t i = 0; i < b.px.size(); i += 4 )
			b.px[ i ] = static_cast< unsigned char >( 255 - b.px[ i ] );
		s.setInput( a );
		const GLuint first = s.input;
		s.input            = 0;
		s.width            = 0;
		s.setInput( b );
		const GLuint second = s.input;
		auto run            = [ & ]( int n ) {
            for( int i = 0; i < n; ++i )
            {
                s.input = ( i & 1 ) ? second : first;
                s.frame();
            }
		};
		run( 20 );
		glFinish();
		const auto start = std::chrono::steady_clock::now();
		run( 60 );
		glFinish();
		const double ms = std::chrono::duration< double >( std::chrono::steady_clock::now() - start ).count() * 1000.0 / 60.0;
		std::printf( "%s   %7.3f       %5.1f%%\n", size.name, ms, ms / 16.667 * 100.0 );
		s.input = first;
		glDeleteTextures( 1, &second );
	}
	return 0;
}

//---------------------------------------------------------------------------
// --out and --pipe share the cue script.
//---------------------------------------------------------------------------
using Track = std::vector< std::pair< int, float > >;

std::map< std::string, Track > loadScript( const std::string& path, std::string& error )
{
	std::map< std::string, Track > tracks;
	std::ifstream file( path );
	if( !file )
	{
		error = "cannot open " + path;
		return tracks;
	}
	std::string line;
	int lineNumber = 0;
	while( std::getline( file, line ) )
	{
		++lineNumber;
		const size_t hash = line.find( '#' );
		if( hash != std::string::npos )
			line.erase( hash );
		std::istringstream in( line );
		int frame = 0;
		if( !( in >> frame ) )
			continue;
		std::vector< std::string > words;
		std::string word;
		while( in >> word )
			words.push_back( word );
		if( words.size() < 2 )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": expected `frame Parameter Name value`";
			return {};
		}
		const float value = std::strtof( words.back().c_str(), nullptr );
		words.pop_back();
		std::string name = words.front();
		for( size_t i = 1; i < words.size(); ++i )
			name += " " + words[ i ];
		tracks[ name ].emplace_back( frame, value );
	}
	for( auto& entry : tracks )
		std::sort( entry.second.begin(), entry.second.end() );
	return tracks;
}

/// A standard parameter ramps between keys. Anything with discrete meaning
/// steps: the value of the latest key at or before the frame.
float valueAt( const Track& track, int frame, bool step )
{
	if( track.empty() )
		return 0.0f;
	if( frame <= track.front().first )
		return track.front().second;
	if( frame >= track.back().first )
		return track.back().second;
	for( size_t i = 1; i < track.size(); ++i )
		if( frame <= track[ i ].first )
		{
			const auto& a = track[ i - 1 ];
			const auto& b = track[ i ];
			if( step )
				return frame >= b.first ? b.second : a.second;
			const float span = static_cast< float >( b.first - a.first );
			const float t    = span > 0.0f ? static_cast< float >( frame - a.first ) / span : 1.0f;
			return a.second + ( b.second - a.second ) * t;
		}
	return track.back().second;
}

struct RunOptions
{
	int width = 640, height = 360;
	int frames  = 120;
	double fps  = 60.0;
	float audio = 0.0f;
	float drift = 0.0f;///< pixels the test card moves left per frame, so a still run has a moving picture
	std::string scriptPath, outPath;
	std::vector< std::pair< std::string, std::string > > sets;
};

struct Cues
{
	struct Bound
	{
		unsigned id;
		bool step;
		Track track;
	};
	std::vector< Bound > bound;

	bool bind( Session& s, const RunOptions& o, std::string& error )
	{
		std::map< std::string, unsigned > byName;
		for( unsigned id = 0; id < PT_ABOUT_TEXT; ++id )
			if( id != PT_AUDIO )
				if( const char* name = s.plugin.GetParamName( id ) )
					byName[ name ] = id;
		for( const auto& kv : o.sets )
		{
			const auto found = byName.find( kv.first );
			if( found == byName.end() )
			{
				error = "no parameter named '" + kv.first + "' (try --list)";
				return false;
			}
			s.plugin.SetFloatParameter( found->second, static_cast< float >( std::atof( kv.second.c_str() ) ) );
		}
		if( o.scriptPath.empty() )
			return true;
		const auto tracks = loadScript( o.scriptPath, error );
		if( !error.empty() )
			return false;
		for( const auto& entry : tracks )
		{
			const auto found = byName.find( entry.first );
			if( found == byName.end() )
			{
				error = "the script names \"" + entry.first + "\", which is not an automatable parameter (try --list)";
				return false;
			}
			const unsigned type = s.plugin.GetParamType( found->second );
			const bool step     = type == FF_TYPE_OPTION || type == FF_TYPE_BOOLEAN || type == FF_TYPE_EVENT || type == FF_TYPE_INTEGER;
			bound.push_back( { found->second, step, entry.second } );
		}
		return true;
	}

	void apply( Session& s, int frame )
	{
		for( const Bound& b : bound )
		{
			if( s.plugin.GetParamType( b.id ) == FF_TYPE_EVENT )
			{
				//A press is a key with value 1 ON that frame, and a release the
				//frame after; a ramp never reaches here.
				bool pressed = false;
				for( const auto& key : b.track )
					pressed = pressed || ( key.first == frame && key.second >= 0.5f );
				s.plugin.SetFloatParameter( b.id, pressed ? 1.0f : 0.0f );
			}
			else
				s.plugin.SetFloatParameter( b.id, valueAt( b.track, frame, b.step ) );
		}
	}
};

int runOut( const RunOptions& o )
{
	if( !openGL() )
		return 1;
	Session s;
	if( !s.init() )
		return 1;
	Cues cues;
	std::string error;
	if( !cues.bind( s, o, error ) )
	{
		std::fprintf( stderr, "fdtest: %s\n", error.c_str() );
		return 2;
	}
	s.fps = o.fps;
	s.setAudio( o.audio );
	const Image still = card( o.width, o.height );
	s.setInput( still );
	Image img;
	for( int f = 0; f < std::max( 1, o.frames ); ++f )
	{
		if( o.drift != 0.0f )
		{
			//The card, wrapped round by drift * f pixels: a moving picture for
			//the controls that only mean anything while the picture moves.
			Image moved     = still;
			const int shift = static_cast< int >( std::lround( o.drift * f ) ) % o.width;
			for( int y = 0; y < o.height; ++y )
				for( int x = 0; x < o.width; ++x )
				{
					const int from = ( ( x + shift ) % o.width + o.width ) % o.width;
					std::memcpy( &moved.px[ ( static_cast< size_t >( y ) * o.width + x ) * 4 ],
					             &still.px[ ( static_cast< size_t >( y ) * o.width + from ) * 4 ], 4 );
				}
			s.setInput( moved );
		}
		cues.apply( s, f );
		img = s.frame();
	}
	if( !writePng( o.outPath, o.width, o.height, img.px ) )
	{
		std::fprintf( stderr, "fdtest: could not write %s\n", o.outPath.c_str() );
		return 1;
	}
	std::printf( "wrote %s (%dx%d, %d frames)\n", o.outPath.c_str(), o.width, o.height, o.frames );
	return 0;
}

int runPipe( const RunOptions& o )
{
	// A reader that hangs up must end the take with exit 1 and a message,
	// not SIGPIPE's silent 141: write() then fails and the loop says so.
	std::signal( SIGPIPE, SIG_IGN );
	if( o.width <= 0 || o.height <= 0 || !( o.fps > 0.0 ) )
	{
		std::fprintf( stderr, "fdtest: --pipe needs a positive size and --fps\n" );
		return 1;
	}
	if( !openGL() )
		return 1;
	Session s;
	if( !s.init() )
		return 1;
	Cues cues;
	std::string error;
	if( !cues.bind( s, o, error ) )
	{
		std::fprintf( stderr, "fdtest: %s\n", error.c_str() );
		return 2;
	}
	s.fps = o.fps;
	s.setAudio( o.audio );
	s.resize( o.width, o.height );

	const size_t bytes = static_cast< size_t >( o.width ) * static_cast< size_t >( o.height ) * 4u;
	Image in;
	in.width  = o.width;
	in.height = o.height;
	in.px.resize( bytes );
	int status   = 0;
	bool noInput = false;
	for( int f = 0; o.frames <= 0 || f < o.frames; ++f )
	{
		if( !noInput )
		{
			size_t filled = 0;
			while( filled < bytes )
			{
				const ssize_t got = read( STDIN_FILENO, in.px.data() + filled, bytes - filled );
				if( got <= 0 )
					break;
				filled += static_cast< size_t >( got );
			}
			if( filled == bytes )
				s.setInput( in );
			else if( f == 0 && filled == 0 && o.frames > 0 )
			{
				//No input at all: film the test card for --frames, so a pipe can
				//be tested without a source.
				noInput = true;
				s.setInput( card( o.width, o.height ) );
			}
			else
				break;//the source ran out, whole frames only
		}
		cues.apply( s, f );
		const Image out = s.frame();
		size_t written  = 0;
		while( written < bytes )
		{
			const ssize_t put = write( STDOUT_FILENO, out.px.data() + written, bytes - written );
			if( put <= 0 )
				break;
			written += static_cast< size_t >( put );
		}
		if( written < bytes )
		{
			std::fprintf( stderr, "fdtest: the reader hung up after %d frame(s)\n", f );
			status = 1;
			break;
		}
	}
	return status;
}

void usage()
{
	std::printf(
		"fdtest -- render and check the Flipdot effect\n"
		"\n"
		"  --out PATH        render the test card through the plugin\n"
		"  --size WxH        (default 640x360)   --frames N (default 120)   --fps N (default 60)\n"
		"  --set \"Name=V\"    set a parameter by its display name (options by index)\n"
		"  --audio LEVEL     write LEVEL into every spectrum bin\n"
		"  --drift PX        move the test card PX pixels a frame (for --out)\n"
		"  --script PATH     cues: 'frame Parameter Name value'\n"
		"  --list | --names\n"
		"  --wipe | --changes | --bistable | --stuck | --rotation | --resize | --prime\n"
		"                    the checks, at 640x360 and 320x180 (FDTEST_RENDERER=software for the software renderer)\n"
		"  --all             every check\n"
		"  --negative        every check against its broken model; each must fail\n"
		"  --bench           720p, 1080p and 4K at the largest sign\n"
		"  --pipe            raw RGBA frames on stdin, raw RGBA frames on stdout\n" );
}

int runCheck( const char* name, void ( *check )( const Debug& ) )
{
	if( !openGL() )
		return 1;
	std::printf( "%s\n", name );
	const int before = gFailures;
	check( Debug {} );
	const int failed = gFailures - before;
	std::printf( "  %s\n", failed == 0 ? "all ok" : "FAILURES" );
	return failed == 0 ? 0 : 1;
}
} // namespace

int main( int argc, char** argv )
{
	RunOptions o;
	bool pipe = false;
	const std::map< std::string, void ( * )( const Debug& ) > checks = {
		{ "--wipe", checkWipe },         { "--changes", checkChanges }, { "--bistable", checkBistable },
		{ "--stuck", checkStuck },       { "--rotation", checkRotation }, { "--resize", checkResize },
		{ "--prime", checkPrime },
	};

	for( int a = 1; a < argc; ++a )
	{
		const std::string arg = argv[ a ];
		auto next             = [ & ]() -> const char* { return a + 1 < argc ? argv[ ++a ] : ""; };
		if( arg == "--help" )
		{
			usage();
			return 0;
		}
		if( checks.count( arg ) )
			return runCheck( arg.c_str(), checks.at( arg ) );
		if( arg == "--all" )
		{
			int status = 0;
			for( const auto& c : checks )
				status |= runCheck( c.first.c_str(), c.second );
			return status;
		}
		if( arg == "--negative" )
			return openGL() ? runNegative() : 1;
		if( arg == "--list" )
			return runList();
		if( arg == "--names" )
			return runNames();
		if( arg == "--bench" )
			return runBench();
		if( arg == "--pipe" )
			pipe = true;
		else if( arg == "--out" )
			o.outPath = next();
		else if( arg == "--size" )
		{
			const std::string v = next();
			const size_t x      = v.find( 'x' );
			if( x == std::string::npos )
			{
				std::fprintf( stderr, "fdtest: --size wants WxH\n" );
				return 2;
			}
			o.width  = std::atoi( v.substr( 0, x ).c_str() );
			o.height = std::atoi( v.substr( x + 1 ).c_str() );
		}
		else if( arg == "--frames" )
			o.frames = std::atoi( next() );
		else if( arg == "--fps" )
			o.fps = std::atof( next() );
		else if( arg == "--audio" )
			o.audio = static_cast< float >( std::atof( next() ) );
		else if( arg == "--drift" )
			o.drift = static_cast< float >( std::atof( next() ) );
		else if( arg == "--script" )
			o.scriptPath = next();
		else if( arg == "--set" )
		{
			const std::string v = next();
			const size_t eq     = v.find( '=' );
			if( eq == std::string::npos )
			{
				std::fprintf( stderr, "fdtest: --set wants Name=Value\n" );
				return 2;
			}
			o.sets.emplace_back( v.substr( 0, eq ), v.substr( eq + 1 ) );
		}
		else
		{
			std::fprintf( stderr, "fdtest: unknown argument %s\n", arg.c_str() );
			usage();
			return 2;
		}
	}

	if( pipe )
		return runPipe( o );
	if( o.outPath.empty() )
		o.outPath = "/tmp/flipdot.png";
	return runOut( o );
}
