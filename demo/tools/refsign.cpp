// The reference side of demo/tools/check_port.mjs.
//
// Built by check_port.mjs against the plugin's own source/Sign.cpp, Disc.cpp,
// Dither.cpp, Controls.cpp, Onset.cpp and Clock.cpp, UNCHANGED, and against
// text CUT OUT of source/Flipdot.h and source/Flipdot.cpp at run time into
// cut_class.inc and cut_defs.inc: the Debug struct and every private member of
// FlipdotPlugin; the anonymous namespace's names, faces, frame clamp,
// intParam and geometryFor; and the constructor, decideUpdate, uploadAngles,
// ProcessOpenGL, SetFloatParameter and SetTime. Nothing in those is retyped.
//
// What IS written here: stand-ins for FFGL, GL and ffglex (demo/tools/stub/
// FFGLSDK.h, which source/Controls.h includes) and PassBuffer and diag, so
// that the constructor and ProcessOpenGL run with no context; and the harness
// below. glReadPixels hands the means pass the floats the script names (the
// means pass is the GPU's and is not in this check); the cos/sin upload and
// every uniform are recorded.
//
//   refsign params                 every declaration the constructor makes
//   refsign laws                   the control laws, the profile, the tone curve, the geometry
//   refsign frames SCRIPT OUTDIR   run a frame script:
//     set INDEX VALUE              SetFloatParameter( INDEX, VALUE ) (a float)
//     frame NOW PICW PICH VPW VPH MEANS   SetTime( NOW ), one ProcessOpenGL
//     dump NAME                    the last upload (cos, sin per disc) to OUTDIR/NAME

#include "Controls.h"
#include "Disc.h"
#include "Dither.h"
#include "Onset.h"
#include "Clock.h"
#include "Sign.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace flipdot
{
namespace diag
{
inline void init() {}
inline void info( const std::string& ) {}
inline void error( const std::string& m )
{
	std::fprintf( stderr, "diag: %s\n", m.c_str() );
}
inline void stateChanged( const char*, const std::string& ) {}
} // namespace diag

/// The pass buffers hold textures the stand-in GL never makes.
struct PassBuffer
{
	enum class Sampling
	{
		Nearest,
		Linear,
		Mipmapped
	};
	bool Ensure( GLsizei, GLsizei, GLint, Sampling )
	{
		return true;
	}
	GLuint GetGLID() const
	{
		return 2;
	}
	GLuint TextureID() const
	{
		return 3;
	}
	void ResizeViewPort() {}
	void GenerateMipmaps() {}
	void Destroy() {}
};
} // namespace flipdot

using namespace ffglex;

namespace flipdot
{
class FlipdotPlugin : public CFFGLPlugin
{
public:
	FlipdotPlugin();
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL );
	FFResult SetFloatParameter( unsigned int index, float value );
	FFResult SetTime( double time );
	FFResult DeInitGL()
	{
		return FF_SUCCESS;
	}

#include "cut_debug.inc"

	void ForceSecondsClock()
	{
		mClock.ForceSeconds();
	}

#include "cut_class.inc"

public:
	const ffglex::FFGLShader& Means() const
	{
		return mMeansShader;
	}
	const ffglex::FFGLShader& Board() const
	{
		return mBoardShader;
	}
	const std::vector< uint8_t >& Target() const
	{
		return mTarget;
	}
	const Sign& SignState() const
	{
		return mSign;
	}
	bool Fired() const
	{
		return mFireThisFrame;
	}
	void ClearUniforms()
	{
		mMeansShader.set.clear();
		mBoardShader.set.clear();
	}
};

#include "cut_defs.inc"

} // namespace flipdot

//---------------------------------------------------------------------------
// The harness.
//---------------------------------------------------------------------------
namespace
{
using namespace flipdot;

std::string num( double v )
{
	char buf[ 40 ];
	std::snprintf( buf, sizeof buf, "%.17g", v );
	return buf;
}

/// Two 32-bit hashes of an array's bytes: FNV-1a, and a multiply-add over
/// 32-bit words (the bytes zero-padded to a whole word). check_port.mjs
/// computes the same two.
std::string hashBytes( const void* data, size_t bytes )
{
	const unsigned char* p = static_cast< const unsigned char* >( data );
	uint32_t a             = 2166136261u;
	for( size_t i = 0; i < bytes; ++i )
	{
		a ^= p[ i ];
		a *= 16777619u;
	}
	uint32_t b = 0x9747B28Cu;
	for( size_t i = 0; i < bytes; i += 4 )
	{
		uint32_t w = 0;
		std::memcpy( &w, p + i, std::min< size_t >( 4, bytes - i ) );
		b = b * 31u + w;
	}
	char buf[ 24 ];
	std::snprintf( buf, sizeof buf, "%08x%08x", a, b );
	return buf;
}

std::string uniforms( const ffglex::FFGLShader& s )
{
	std::string out;
	for( const auto& [ name, values ] : s.set )
	{
		if( name == "Picture" || name == "Turn" )
			continue;
		out += name + "=";
		for( size_t i = 0; i < values.size(); ++i )
			out += ( i ? "," : "" ) + num( values[ i ] );
		out += " ";
	}
	return out;
}

const char* typeName( unsigned int type )
{
	switch( type )
	{
	case FF_TYPE_BOOLEAN: return "boolean";
	case FF_TYPE_EVENT: return "event";
	case FF_TYPE_STANDARD: return "standard";
	case FF_TYPE_OPTION: return "option";
	case FF_TYPE_BUFFER: return "buffer";
	case FF_TYPE_INTEGER: return "integer";
	case FF_TYPE_TEXT: return "text";
	default: return "unknown";
	}
}

int runParams()
{
	FlipdotPlugin plugin;
	for( const auto& p : plugin.declared )
	{
		std::printf( "param %u|%s|%s|%s|%s", p.id, p.name.c_str(), typeName( p.type ), p.group.c_str(), num( p.defaultValue ).c_str() );
		std::printf( "|%s", p.ranged ? ( num( p.min ) + "," + num( p.max ) ).c_str() : "" );
		std::string elements;
		if( p.type == FF_TYPE_OPTION )
			for( size_t i = 0; i < p.elements.size(); ++i )
				elements += ( i ? ";" : "" ) + p.elements[ i ].name + "=" + num( p.elements[ i ].value );
		else if( p.type == FF_TYPE_BUFFER )
			elements = std::to_string( p.elements.size() ) + " bins";
		std::printf( "|%s\n", elements.c_str() );
	}
	return 0;
}

std::vector< float > lawValues()
{
	std::vector< float > v;
	for( int i = 0; i <= 1000; ++i )
		v.push_back( static_cast< float >( i ) / 1000.0f );
	for( float x : { -0.5f, -1e-9f, 1.0000001f, 1.5f, 2.0f, 3.0f, 0.49999997f, 1e-4f, 0.33333334f, 0.6666667f, 0.99995f } )
		v.push_back( x );
	return v;
}

int runLaws()
{
	for( float v : lawValues() )
	{
		std::printf( "law %s", num( v ).c_str() );
		const double outs[] = {
			static_cast< double >( OptionIndex( v, 2 ) ), static_cast< double >( OptionIndex( v, 3 ) ), static_cast< double >( OptionIndex( v, 4 ) ),
			static_cast< double >( intParam( v * 200.0f, kColumnsMin, kColumnsMax ) ),
			DiscSizeFromParam( v ), ScanRateFromParam( v ), IntervalFromParam( v ), FlipTimeFromParam( v ),
			ReboundFromParam( v ), StuckFromParam( v ), LateFromParam( v ), LightAngleFromParam( v ),
			ScanRateToParam( ScanRateFromParam( v ) ), IntervalToParam( IntervalFromParam( v ) ), FlipTimeToParam( FlipTimeFromParam( v ) ),
			ReboundToParam( ReboundFromParam( v ) ), StuckToParam( StuckFromParam( v ) ), LateToParam( LateFromParam( v ) ),
			ToneCurve( v, 0.25 ), ToneCurve( v, 0.5 ), ToneCurve( v, 0.0 ), ToneCurve( v, static_cast< double >( v ) ),
		};
		for( double o : outs )
			std::printf( " %s", num( o ).c_str() );
		std::printf( "\n" );
	}
	// The last two sit just under the half-degree cut for their second and
	// third rebounds (e^2k = 1 / 180.25 against 1 / 180), so a cut moved by
	// a hair shows.
	for( double e : { 0.0, 0.05, 0.1, 0.18, 0.3, 0.45, 0.6, std::pow( 180.25, -0.25 ), std::pow( 180.25, -1.0 / 6.0 ) } )
	{
		std::printf( "profile %s %s", num( e ).c_str(), num( disc::ReboundEnd( e ) ).c_str() );
		// 4,001 points from before the pulse to well past the train.
		std::vector< double > samples;
		for( int k = 0; k <= 4000; ++k )
		{
			const double u = static_cast< double >( k - 250 ) / 1000.0;//no multiply-add to fuse
			samples.push_back( disc::SwingAngle( u, e ) );
			samples.push_back( disc::ReboundDepth( u, e ) );
		}
		std::printf( " %s\n", hashBytes( samples.data(), samples.size() * sizeof( double ) ).c_str() );
	}
	for( int w : { 320, 640, 721, 1280, 1920, 3840 } )
		for( int h : { 180, 360, 405, 720, 1080, 2160 } )
			for( int c : { 4, 17, 64, 100, 192 } )
				for( int r : { 2, 9, 36, 108 } )
					for( int off = 0; off < 2; ++off )
					{
						const Geometry g = geometryFor( w, h, c, r, off == 1 );
						std::printf( "geometry %d %d %d %d %d %s %s %s %s %s\n", w, h, c, r, off, num( g.pitch ).c_str(), num( g.originX ).c_str(), num( g.originY ).c_str(), num( g.width ).c_str(), num( g.height ).c_str() );
					}
	return 0;
}

bool readFloats( const std::string& path, std::vector< float >& out )
{
	std::ifstream in( path, std::ios::binary | std::ios::ate );
	if( !in )
		return false;
	const std::streamsize bytes = in.tellg();
	in.seekg( 0 );
	out.resize( static_cast< size_t >( bytes ) / sizeof( float ) );
	in.read( reinterpret_cast< char* >( out.data() ), bytes );
	return static_cast< bool >( in );
}

int runFrames( const std::string& script, const std::string& outdir )
{
	std::ifstream in( script );
	if( !in )
	{
		std::fprintf( stderr, "no script %s\n", script.c_str() );
		return 2;
	}
	FlipdotPlugin plugin;
	plugin.ForceSecondsClock();
	FFGLTextureStruct picture {};
	FFGLTextureStruct* inputs[ 1 ] = { &picture };
	ProcessOpenGLStruct gl {};
	gl.numInputTextures = 1;
	gl.inputTextures    = inputs;

	std::string line;
	int frame = 0;
	while( std::getline( in, line ) )
	{
		std::istringstream words( line );
		std::string verb;
		words >> verb;
		if( verb == "set" )
		{
			unsigned int index = 0;
			float value        = 0.0f;
			words >> index >> value;
			plugin.SetFloatParameter( index, value );
		}
		else if( verb == "frame" )
		{
			double now = 0.0;
			int pw = 0, ph = 0, vw = 0, vh = 0;
			std::string means;
			words >> now >> pw >> ph >> vw >> vh >> means;
			if( !readFloats( means, stub::means ) )
			{
				std::fprintf( stderr, "no means %s\n", means.c_str() );
				return 2;
			}
			stub::readMismatch = false;
			stub::readCount    = 0;
			stub::uniformRefs.clear();
			stub::viewportW        = vw;
			stub::viewportH        = vh;
			picture.Width          = static_cast< FFUInt32 >( pw );
			picture.Height         = static_cast< FFUInt32 >( ph );
			picture.HardwareWidth  = static_cast< FFUInt32 >( pw );
			picture.HardwareHeight = static_cast< FFUInt32 >( ph );
			picture.Handle         = 9;
			plugin.ClearUniforms();
			plugin.SetTime( now );
			const FFResult r         = plugin.ProcessOpenGL( &gl );
			const Sign& sign         = plugin.SignState();
			const std::vector< float >& angles = sign.Angles();
			const std::vector< uint8_t >& target = plugin.Target();
			std::printf( "frame %d result=%u read=%s means=%d fired=%d grid=%dx%d stuck=%d active=%d angles=%s turn=%s target=%s | %s| %s\n",
			             frame, r, stub::readMismatch ? "MISMATCH" : "ok", stub::readCount, plugin.Fired() ? 1 : 0,
			             sign.Columns(), sign.Rows(), sign.StuckCount(), sign.PassActive() ? 1 : 0,
			             hashBytes( angles.data(), angles.size() * sizeof( float ) ).c_str(),
			             hashBytes( stub::turn.data(), stub::turn.size() * sizeof( float ) ).c_str(),
			             hashBytes( target.data(), target.size() ).c_str(),
			             uniforms( plugin.Means() ).c_str(), uniforms( plugin.Board() ).c_str() );
			++frame;
		}
		else if( verb == "dump" )
		{
			std::string name;
			words >> name;
			std::ofstream out( outdir + "/" + name, std::ios::binary );
			out.write( reinterpret_cast< const char* >( stub::turn.data() ), static_cast< std::streamsize >( stub::turn.size() * sizeof( float ) ) );
		}
	}
	return 0;
}
} // namespace

int main( int argc, char** argv )
{
	const std::string mode = argc > 1 ? argv[ 1 ] : "";
	if( mode == "params" )
		return runParams();
	if( mode == "laws" )
		return runLaws();
	if( mode == "frames" && argc > 3 )
		return runFrames( argv[ 2 ], argv[ 3 ] );
	std::fprintf( stderr, "usage: refsign params | laws | frames SCRIPT OUTDIR\n" );
	return 2;
}
