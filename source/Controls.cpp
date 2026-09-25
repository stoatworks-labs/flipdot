#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace flipdot
{
namespace
{
inline double clamp01( double value )
{
	return std::min( std::max( value, 0.0 ), 1.0 );
}

inline double lerp( double from, double to, double t )
{
	return from + ( to - from ) * clamp01( t );
}

inline double lerpInverse( double from, double to, double x )
{
	return clamp01( ( x - from ) / ( to - from ) );
}

/// Geometric interpolation: equal slider movements are equal *ratios*, right
/// for any quantity where the question is "how many times more".
inline double geometric( double from, double to, double t )
{
	return from * std::pow( to / from, clamp01( t ) );
}

inline double geometricInverse( double from, double to, double x )
{
	return clamp01( std::log( x / from ) / std::log( to / from ) );
}

constexpr double kScanMin = 4.0, kScanMax = 4000.0;
constexpr double kIntervalMin = 0.1, kIntervalMax = 10.0;
constexpr double kFlipMin = 0.005, kFlipMax = 1.0;
constexpr double kReboundMax  = 0.6;
constexpr double kStuckMax    = 0.2;
constexpr double kLateMax     = 0.5;
} // namespace

int OptionIndex( float value, int count )
{
	if( count <= 1 )
		return 0;
	// An index arrives as 0, 1, 2...; a normalised value as 0..1. Anything at
	// or below 1 that is not a whole number can only be the latter.
	float v = value;
	if( v > 0.0f && v <= 1.0f && std::fabs( v - std::round( v ) ) > 1e-4f )
		v = v * static_cast< float >( count - 1 );
	const int index = static_cast< int >( std::lround( v ) );
	return std::min( std::max( index, 0 ), count - 1 );
}

float DiscSizeFromParam( float value )
{
	return static_cast< float >( lerp( 0.4, 1.0, value ) );
}

double ScanRateFromParam( float value )
{
	return geometric( kScanMin, kScanMax, value );
}

float ScanRateToParam( double linesPerSecond )
{
	return static_cast< float >( geometricInverse( kScanMin, kScanMax, linesPerSecond ) );
}

double IntervalFromParam( float value )
{
	return geometric( kIntervalMin, kIntervalMax, value );
}

float IntervalToParam( double seconds )
{
	return static_cast< float >( geometricInverse( kIntervalMin, kIntervalMax, seconds ) );
}

double FlipTimeFromParam( float value )
{
	return geometric( kFlipMin, kFlipMax, value );
}

float FlipTimeToParam( double seconds )
{
	return static_cast< float >( geometricInverse( kFlipMin, kFlipMax, seconds ) );
}

double ReboundFromParam( float value )
{
	return lerp( 0.0, kReboundMax, value );
}

float ReboundToParam( double restitution )
{
	return static_cast< float >( lerpInverse( 0.0, kReboundMax, restitution ) );
}

double StuckFromParam( float value )
{
	return lerp( 0.0, kStuckMax, value );
}

float StuckToParam( double fraction )
{
	return static_cast< float >( lerpInverse( 0.0, kStuckMax, fraction ) );
}

double LateFromParam( float value )
{
	return lerp( 0.0, kLateMax, value );
}

float LateToParam( double fraction )
{
	return static_cast< float >( lerpInverse( 0.0, kLateMax, fraction ) );
}

float LightAngleFromParam( float value )
{
	constexpr double kEightyDegrees = 1.3962634015954636;
	return static_cast< float >( lerp( 0.0, kEightyDegrees, value ) );
}

} // namespace flipdot
