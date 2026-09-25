#include "Flipdot.h"

#include "Diag.h"
#include "Disc.h"
#include "Dither.h"
#include "Shaders.h"

//FFGLSDK.h includes every other scoped binding and omits this one (SDK
//b1afaf9), so it has to be asked for by name.
#include <ffglex/FFGLScopedFBOBinding.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace ffglex;

namespace flipdot
{
namespace
{
static_assert( PT_COUNT_ - PT_ABOUT_TEXT == stoatworks::about::kParamCount,
               "the About block's size changed (a user guide was added?): add or remove a PT_ABOUT_BUTTON_n" );

const char* const kLayoutNames[ kLayoutCount ] = { "Square", "Offset" };
const char* const kColourNames[ kColourCount ] = { "Yellow", "Green", "White" };
const char* const kScanNames[ kScanCount ]     = { "Column by Column", "Row by Row" };
const char* const kUpdateNames[ kUpdateCount ] = { "Continuous", "Interval", "Onset", "Manual" };
const char* const kDitherNames[ kDitherCount ] = { "Threshold", "Bayer 4x4", "Floyd-Steinberg" };

/// The fluorescent face of each Colour, in the output's encoding.
const float kFaces[ kColourCount ][ 3 ] = {
	{ 1.00f, 0.84f, 0.06f },//the classic daylight-fluorescent yellow
	{ 0.55f, 1.00f, 0.18f },//fluorescent green
	{ 0.94f, 0.94f, 0.90f },//white
};

/// Seconds of host time a single frame may advance the sign by. The host's
/// clock jumps on a scrub, a retrigger and a sleep; unclamped, any of those
/// turns into a sweep finishing in one frame.
constexpr double kMaxFrameDelta = 0.25;

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

int intParam( float value, int lo, int hi )
{
	return std::min( std::max( static_cast< int >( std::lround( value ) ), lo ), hi );
}

/// Where the sign sits in a picture of this size: square pitch, as large as
/// fits, centred. Offset rows need half a pitch more width.
struct Geometry
{
	float pitch = 1.0f, originX = 0.0f, originY = 0.0f, width = 0.0f, height = 0.0f;
};

Geometry geometryFor( int pictureWidth, int pictureHeight, int columns, int rows, bool offset )
{
	Geometry g;
	const float across = static_cast< float >( columns ) + ( offset ? 0.5f : 0.0f );
	g.pitch            = std::min( static_cast< float >( pictureWidth ) / across, static_cast< float >( pictureHeight ) / static_cast< float >( rows ) );
	g.width            = g.pitch * across;
	g.height           = g.pitch * static_cast< float >( rows );
	g.originX          = 0.5f * ( static_cast< float >( pictureWidth ) - g.width );
	g.originY          = 0.5f * ( static_cast< float >( pictureHeight ) - g.height );
	return g;
}
} // namespace

FlipdotPlugin::FlipdotPlugin()
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );
	SetTimeSupported( true );

	//---------------------------------------------------------------------
	// Defaults: a 64 x 36 sign of yellow discs that fills a 16:9 frame,
	// scanned column by column at 240 a second (a sweep in 0.27 s), chasing
	// the clip continuously and pulsing only what changes, 40 ms a swing with
	// a small rebound, a few dead dots, Bayer dithered, lit a little from the
	// left.
	//---------------------------------------------------------------------
	mParams[ PT_COLUMNS ]   = static_cast< float >( kColumnsDefault );
	mParams[ PT_ROWS ]      = static_cast< float >( kRowsDefault );
	mParams[ PT_LAYOUT ]    = static_cast< float >( kLayoutSquare );
	mParams[ PT_DISC_SIZE ] = 0.85f;//0.91 of the pitch
	mParams[ PT_COLOUR ]    = static_cast< float >( kColourYellow );

	mParams[ PT_SCAN ]         = static_cast< float >( kScanColumns );
	mParams[ PT_SCAN_RATE ]    = ScanRateToParam( 240.0 );
	mParams[ PT_UPDATE ]       = static_cast< float >( kUpdateContinuous );
	mParams[ PT_INTERVAL ]     = IntervalToParam( 2.0 );
	mParams[ PT_ONLY_CHANGES ] = 1.0f;

	mParams[ PT_FLIP_TIME ] = FlipTimeToParam( 0.04 );
	mParams[ PT_REBOUND ]   = ReboundToParam( 0.3 );
	mParams[ PT_STUCK ]     = StuckToParam( 0.003 );
	mParams[ PT_LATE ]      = LateToParam( 0.02 );

	mParams[ PT_DITHER ]    = static_cast< float >( kDitherBayer );
	mParams[ PT_THRESHOLD ] = 0.25f;
	mParams[ PT_LIGHT ]     = 0.3f;//24 degrees from the left
	mParams[ PT_MIX ]       = 1.0f;

	//---------------------------------------------------------------------
	// Declaration. Every FF_TYPE_STANDARD is 0..1 (SetParamInfo clamps the
	// default before a range could be attached); the counts are real
	// integers, which the clamp does not touch.
	//---------------------------------------------------------------------
	SetParamInfo( PT_COLUMNS, "Columns", FF_TYPE_INTEGER, mParams[ PT_COLUMNS ] );
	SetParamRange( PT_COLUMNS, static_cast< float >( kColumnsMin ), static_cast< float >( kColumnsMax ) );
	SetParamInfo( PT_ROWS, "Rows", FF_TYPE_INTEGER, mParams[ PT_ROWS ] );
	SetParamRange( PT_ROWS, static_cast< float >( kRowsMin ), static_cast< float >( kRowsMax ) );
	SetOptionParamInfo( PT_LAYOUT, "Layout", kLayoutCount, mParams[ PT_LAYOUT ] );
	for( int i = 0; i < kLayoutCount; ++i )
		SetParamElementInfo( PT_LAYOUT, static_cast< unsigned >( i ), kLayoutNames[ i ], static_cast< float >( i ) );
	SetParamInfo( PT_DISC_SIZE, "Disc Size", FF_TYPE_STANDARD, mParams[ PT_DISC_SIZE ] );
	SetOptionParamInfo( PT_COLOUR, "Colour", kColourCount, mParams[ PT_COLOUR ] );
	for( int i = 0; i < kColourCount; ++i )
		SetParamElementInfo( PT_COLOUR, static_cast< unsigned >( i ), kColourNames[ i ], static_cast< float >( i ) );

	SetOptionParamInfo( PT_SCAN, "Scan", kScanCount, mParams[ PT_SCAN ] );
	for( int i = 0; i < kScanCount; ++i )
		SetParamElementInfo( PT_SCAN, static_cast< unsigned >( i ), kScanNames[ i ], static_cast< float >( i ) );
	SetParamInfo( PT_SCAN_RATE, "Scan Rate", FF_TYPE_STANDARD, mParams[ PT_SCAN_RATE ] );
	SetOptionParamInfo( PT_UPDATE, "Update", kUpdateCount, mParams[ PT_UPDATE ] );
	for( int i = 0; i < kUpdateCount; ++i )
		SetParamElementInfo( PT_UPDATE, static_cast< unsigned >( i ), kUpdateNames[ i ], static_cast< float >( i ) );
	SetParamInfo( PT_INTERVAL, "Interval", FF_TYPE_STANDARD, mParams[ PT_INTERVAL ] );
	SetParamInfo( PT_ONLY_CHANGES, "Only Changes", FF_TYPE_BOOLEAN, true );
	SetParamInfo( PT_UPDATE_NOW, "Update Now", FF_TYPE_EVENT, false );

	// An FFT buffer: Resolume shows it as an audio-source picker and writes
	// one spectrum bin per element. With no audio routed Onset never fires.
	SetBufferParamInfo( PT_AUDIO, "Audio", kAudioBins, FF_USAGE_FFT );
	for( int i = 0; i < kAudioBins; ++i )
		SetParamElementInfo( PT_AUDIO, static_cast< unsigned >( i ), "", 0.0f );

	SetParamInfo( PT_FLIP_TIME, "Flip Time", FF_TYPE_STANDARD, mParams[ PT_FLIP_TIME ] );
	SetParamInfo( PT_REBOUND, "Rebound", FF_TYPE_STANDARD, mParams[ PT_REBOUND ] );
	SetParamInfo( PT_STUCK, "Stuck", FF_TYPE_STANDARD, mParams[ PT_STUCK ] );
	SetParamInfo( PT_LATE, "Late", FF_TYPE_STANDARD, mParams[ PT_LATE ] );

	SetOptionParamInfo( PT_DITHER, "Dither", kDitherCount, mParams[ PT_DITHER ] );
	for( int i = 0; i < kDitherCount; ++i )
		SetParamElementInfo( PT_DITHER, static_cast< unsigned >( i ), kDitherNames[ i ], static_cast< float >( i ) );
	SetParamInfo( PT_THRESHOLD, "Threshold", FF_TYPE_STANDARD, mParams[ PT_THRESHOLD ] );
	SetParamInfo( PT_LIGHT, "Light", FF_TYPE_STANDARD, mParams[ PT_LIGHT ] );
	SetParamInfo( PT_MIX, "Mix", FF_TYPE_STANDARD, mParams[ PT_MIX ] );

	for( FFUInt32 i = PT_COLUMNS; i <= PT_COLOUR; ++i )
		SetParamGroup( i, "Sign" );
	for( FFUInt32 i = PT_SCAN; i <= PT_AUDIO; ++i )
		SetParamGroup( i, "Driver" );
	for( FFUInt32 i = PT_FLIP_TIME; i <= PT_LATE; ++i )
		SetParamGroup( i, "Discs" );
	for( FFUInt32 i = PT_DITHER; i <= PT_MIX; ++i )
		SetParamGroup( i, "Look" );

	// The About block. Inline, because SetParamInfo is protected.
	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_TEXT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_TEXT; i < PT_COUNT_; ++i )
		SetParamGroup( i, "About" );

	FFGLLog::LogToHost( "Created Flipdot effect" );
	diag::init();
}

//---------------------------------------------------------------------------
void FlipdotPlugin::SetDebugForTest( const Debug& debug )
{
	mDebug               = debug;
	mSign.debug          = debug.sign;
	mOnset.debug.noPrime = debug.noPrime;
}

//---------------------------------------------------------------------------
FFResult FlipdotPlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR )
	            + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	struct
	{
		FFGLShader* shader;
		const char* fragment;
		const char* name;
	} const stages[] = {
		{ &mCopyShader, kCopyShader, "copy" },
		{ &mMeansShader, kMeansShader, "means" },
		{ &mBoardShader, kBoardShader, "board" },
	};

	for( const auto& stage : stages )
	{
		if( stage.shader->Compile( kVertexShader, stage.fragment ) )
			continue;
		//Returning FF_FAIL here is invisible to the operator: the effect
		//simply does nothing in Resolume. These lines are the only record.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the effect will do nothing" );
		FFGLLog::LogToHost( "Flipdot: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	if( !mQuad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}

	//The sign itself is NOT reset here. It is bistable: a clip retrigger
	//re-initialises the GL side, and the discs stay where they were.
	mFirstFrame  = true;
	mLastSeconds = -1.0;
	mOnset.Reset();
	mNextTick       = -1.0;
	mLastUpdateMode = -1;

	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
void FlipdotPlugin::decideUpdate( double now )
{
	const int mode = OptionIndex( mParams[ PT_UPDATE ], kUpdateCount );
	bool fire      = mUpdatePending;
	mUpdatePending = false;

	if( mode != mLastUpdateMode )
	{
		mLastUpdateMode = mode;
		mNextTick       = -1.0;
	}

	switch( mode )
	{
	case kUpdateInterval:
	{
		//Interval picks the clip up on its first frame; then every Interval.
		const double interval = IntervalFromParam( mParams[ PT_INTERVAL ] );
		if( mFirstFrame )
			fire = true;
		if( mNextTick < 0.0 )
			mNextTick = now + interval;
		else if( now >= mNextTick )
		{
			fire = true;
			mNextTick += interval;
			if( now >= mNextTick )//a stall longer than the interval: one update, not a burst
				mNextTick = now + interval;
		}
		break;
	}
	case kUpdateOnset:
		//Never on the first frame by itself: the detector is primed there and
		//cannot fire, so a clip triggered into loud audio holds the sign.
		if( mOnsetFired )
			fire = true;
		break;
	default:
		break;
	}
	mFireThisFrame = fire;
}

//---------------------------------------------------------------------------
bool FlipdotPlugin::uploadAngles()
{
	//The shader is handed cos and sin, worked out here in double, and does no
	//trigonometry of its own: GLSL 4.10 leaves the precision of cos and sin
	//to the implementation, and Apple's software renderer is out by 1e-3 near
	//edge-on, a tenth of a pixel of disc width at r = 72.
	const int columns = mSign.Columns(), rows = mSign.Rows();
	const std::vector< float >& angles = mSign.Angles();
	mTurn.resize( angles.size() * 2 );
	for( size_t i = 0; i < angles.size(); ++i )
	{
		const double phi = static_cast< double >( angles[ i ] );
		mTurn[ 2 * i ]     = static_cast< float >( std::cos( phi ) );
		mTurn[ 2 * i + 1 ] = static_cast< float >( std::sin( phi ) );
	}
	if( mAngleTexture == 0 )
		glGenTextures( 1, &mAngleTexture );
	glBindTexture( GL_TEXTURE_2D, mAngleTexture );
	if( columns != mAngleColumns || rows != mAngleRows )
	{
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RG32F, columns, rows, 0, GL_RG, GL_FLOAT, mTurn.data() );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		mAngleColumns = columns;
		mAngleRows    = rows;
	}
	else
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, columns, rows, GL_RG, GL_FLOAT, mTurn.data() );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return mAngleTexture != 0;
}

//---------------------------------------------------------------------------
FFResult FlipdotPlugin::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;

	const FFGLTextureStruct& picture = *pGL->inputTextures[ 0 ];
	if( picture.Width == 0 || picture.Height == 0 )
		return FF_FAIL;

	const int pictureWidth  = static_cast< int >( picture.Width );
	const int pictureHeight = static_cast< int >( picture.Height );

	//The host's viewport, read before anything of ours changes it.
	//ScopedFBOBinding restores the framebuffer binding and only that.
	GLint hostViewport[ 4 ] = { 0, 0, 0, 0 };
	glGetIntegerv( GL_VIEWPORT, hostViewport );

	//---------------------------------------------------------------------
	// Time, in seconds whatever the host sends. Only the frame's delta goes
	// anywhere: the driver and the discs keep every time relative to now,
	// in double, so Resolume's ~499 million ms clock never reaches a float.
	//---------------------------------------------------------------------
	mClock.Tick( mHostTime, mHostTimeSeen );
	const double now = mClock.Seconds();
	double dt        = 0.0;
	if( mLastSeconds >= 0.0 )
		dt = std::min( std::max( now - mLastSeconds, 0.0 ), kMaxFrameDelta );
	mLastSeconds = now;

	//---------------------------------------------------------------------
	// Audio, and whether an update fires this frame.
	//---------------------------------------------------------------------
	{
		float bins[ kAudioBins ] = {};
		bool any                 = false;
		if( const ParamInfo* info = FindParamInfo( PT_AUDIO ) )
		{
			const size_t n = std::min( info->elements.size(), static_cast< size_t >( kAudioBins ) );
			for( size_t i = 0; i < n; ++i )
			{
				bins[ i ] = info->elements[ i ].value;
				any       = any || bins[ i ] > 0.0f;
			}
		}
		if( any && !mAudioSeen )
		{
			mAudioSeen = true;
			diag::info( "audio reached the plugin" );
		}
		mOnsetFired = mOnset.Frame( now, bins, kAudioBins );
	}
	decideUpdate( now );
	const int mode        = OptionIndex( mParams[ PT_UPDATE ], kUpdateCount );
	const bool continuous = mode == kUpdateContinuous;

	//---------------------------------------------------------------------
	// The sign's size and where it sits.
	//---------------------------------------------------------------------
	const int columns = intParam( mParams[ PT_COLUMNS ], kColumnsMin, kColumnsMax );
	const int rows    = intParam( mParams[ PT_ROWS ], kRowsMin, kRowsMax );
	const bool offset = OptionIndex( mParams[ PT_LAYOUT ], kLayoutCount ) == kLayoutOffset;
	mSign.Configure( columns, rows, StuckFromParam( mParams[ PT_STUCK ] ), LateFromParam( mParams[ PT_LATE ] ) );

	const bool pictureResized = pictureWidth != mPictureWidth || pictureHeight != mPictureHeight;
	if( pictureResized && mPictureWidth > 0 && mDebug.clearOnResize )
		mSign.Reset();
	mPictureWidth  = pictureWidth;
	mPictureHeight = pictureHeight;

	const Geometry inGeometry  = geometryFor( pictureWidth, pictureHeight, columns, rows, offset );
	const Geometry outGeometry = geometryFor( hostViewport[ 2 ], hostViewport[ 3 ], columns, rows, offset );
	const float pictureLod     = std::floor( std::log2( std::max( 1.0f, inGeometry.pitch / 8.0f ) ) );

	//---------------------------------------------------------------------
	// Buffers, before anything binds a texture: allocating one unbinds the
	// active unit (every ffglex Scoped* clears to 0 on exit, not restores).
	//---------------------------------------------------------------------
	if( !mCopy.Ensure( pictureWidth, pictureHeight, GL_RGBA8, PassBuffer::Sampling::Mipmapped )
	    || !mMeans.Ensure( columns, rows, GL_R32F, PassBuffer::Sampling::Nearest ) )
	{
		diag::error( "could not allocate a buffer" );
		return FF_FAIL;
	}

	//---------------------------------------------------------------------
	// 1. The clip, into a texture of ours, with a mip chain.
	//---------------------------------------------------------------------
	{
		ScopedFBOBinding fbo( mCopy.GetGLID(), ScopedFBOBinding::RB_REVERT );
		mCopy.ResizeViewPort();
		ScopedShaderBinding shader( mCopyShader.GetGLID() );
		ScopedSamplerActivation sampler( 0 );
		Scoped2DTextureBinding texture( picture.Handle );

		const FFGLTexCoords maxCoords = GetMaxGLTexCoords( picture );
		mCopyShader.Set( "InputTexture", 0 );
		mCopyShader.Set( "MaxUV", maxCoords.s, maxCoords.t );
		mQuad.Draw();
	}
	mCopy.GenerateMipmaps();

	//---------------------------------------------------------------------
	// 2. Each disc's mean luma, and the target, when a pass needs one: every
	// frame when continuous, else only on an update.
	//---------------------------------------------------------------------
	const size_t discs = static_cast< size_t >( columns ) * static_cast< size_t >( rows );
	if( continuous || mFireThisFrame || mTarget.size() != discs )
	{
		{
			ScopedFBOBinding fbo( mMeans.GetGLID(), ScopedFBOBinding::RB_REVERT );
			mMeans.ResizeViewPort();
			ScopedShaderBinding shader( mMeansShader.GetGLID() );
			ScopedSamplerActivation sampler( 0 );
			Scoped2DTextureBinding pictureBinding( mCopy.TextureID() );

			mMeansShader.Set( "Picture", 0 );
			glUniform2i( mMeansShader.FindUniform( "Grid" ), columns, rows );
			mMeansShader.Set( "OffsetRows", offset ? 1 : 0 );
			mMeansShader.Set( "BoardOrigin", inGeometry.originX, inGeometry.originY );
			mMeansShader.Set( "Pitch", inGeometry.pitch );
			mMeansShader.Set( "PictureSize", static_cast< float >( pictureWidth ), static_cast< float >( pictureHeight ) );
			mMeansShader.Set( "PictureLod", pictureLod );
			mQuad.Draw();

			mLuma.resize( discs );
			glReadPixels( 0, 0, columns, rows, GL_RED, GL_FLOAT, mLuma.data() );
		}
		DitherGrid( mLuma, columns, rows, OptionIndex( mParams[ PT_DITHER ], kDitherCount ), mParams[ PT_THRESHOLD ], mTarget );
	}

	//---------------------------------------------------------------------
	// 3. The driver and the discs, on the CPU in double.
	//---------------------------------------------------------------------
	Sign::Settings settings;
	settings.continuous  = continuous;
	settings.scanRows    = OptionIndex( mParams[ PT_SCAN ], kScanCount ) == kScanRows;
	settings.scanRate    = ScanRateFromParam( mParams[ PT_SCAN_RATE ] );
	settings.onlyChanges = mParams[ PT_ONLY_CHANGES ] > 0.5f;
	settings.flipTime    = FlipTimeFromParam( mParams[ PT_FLIP_TIME ] );
	settings.restitution = ReboundFromParam( mParams[ PT_REBOUND ] );
	mSign.Advance( dt, mFireThisFrame, mTarget, settings );
	mFirstFrame = false;

	if( !uploadAngles() )
		return FF_FAIL;

	//---------------------------------------------------------------------
	// 4. The board, straight to the host's framebuffer.
	//---------------------------------------------------------------------
	{
		glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );

		ScopedShaderBinding shader( mBoardShader.GetGLID() );
		ScopedSamplerActivation sampler0( 0 );
		Scoped2DTextureBinding pictureBinding( mCopy.TextureID() );
		ScopedSamplerActivation sampler1( 1 );
		Scoped2DTextureBinding angleBinding( mAngleTexture );

		const int colour = OptionIndex( mParams[ PT_COLOUR ], kColourCount );
		mBoardShader.Set( "Picture", 0 );
		mBoardShader.Set( "Turn", 1 );
		glUniform2i( mBoardShader.FindUniform( "Grid" ), columns, rows );
		mBoardShader.Set( "OffsetRows", offset ? 1 : 0 );
		mBoardShader.Set( "BoardOrigin", outGeometry.originX, outGeometry.originY );
		mBoardShader.Set( "BoardSize", outGeometry.width, outGeometry.height );
		mBoardShader.Set( "Pitch", outGeometry.pitch );
		mBoardShader.Set( "OutSize", static_cast< float >( hostViewport[ 2 ] ), static_cast< float >( hostViewport[ 3 ] ) );
		mBoardShader.Set( "DiscSize", DiscSizeFromParam( mParams[ PT_DISC_SIZE ] ) );
		mBoardShader.Set( "FaceColour", kFaces[ colour ][ 0 ], kFaces[ colour ][ 1 ], kFaces[ colour ][ 2 ] );
		const double light = static_cast< double >( LightAngleFromParam( mParams[ PT_LIGHT ] ) );
		mBoardShader.Set( "LightSinCos", static_cast< float >( std::sin( light ) ), static_cast< float >( std::cos( light ) ) );
		mBoardShader.Set( "MixAmount", mParams[ PT_MIX ] );
		mQuad.Draw();
	}

	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult FlipdotPlugin::DeInitGL()
{
	mCopyShader.FreeGLResources();
	mMeansShader.FreeGLResources();
	mBoardShader.FreeGLResources();
	mQuad.Release();
	mCopy.Destroy();
	mMeans.Destroy();
	if( mAngleTexture != 0 )
	{
		glDeleteTextures( 1, &mAngleTexture );
		mAngleTexture = 0;
	}
	mAngleColumns = mAngleRows = 0;
	mFirstFrame                = true;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult FlipdotPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT_ )
		return FF_FAIL;

	if( index >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( index - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	if( index == PT_UPDATE_NOW )
	{
		//An event arrives as 1.0 on press and 0.0 on release.
		if( value >= 0.5f )
			mUpdatePending = true;
		return FF_SUCCESS;
	}

	mParams[ index ] = value;
	return FF_SUCCESS;
}

float FlipdotPlugin::GetFloatParameter( unsigned int index )
{
	if( index >= PT_COUNT_ )
		return 0.0f;
	return mParams[ index ];
}

FFResult FlipdotPlugin::SetTextParameter( unsigned int index, const char* value )
{
	//The About line is display-only, but the base class fails, and a failed
	//default deletes the instance in a real host.
	if( index == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

char* FlipdotPlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_TEXT )
	{
		mAboutText = stoatworks::about::textParam( 0 );
		return const_cast< char* >( mAboutText.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult FlipdotPlugin::SetTime( double time )
{
	mHostTime     = time;
	mHostTimeSeen = true;
	return FF_SUCCESS;
}

} // namespace flipdot
