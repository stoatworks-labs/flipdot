#include "Dither.h"

#include "Controls.h"

#include <algorithm>

namespace flipdot
{
namespace
{
/// The 4 x 4 Bayer index matrix, 0..15.
constexpr int kBayer[ 4 ][ 4 ] = {
	{ 0, 8, 2, 10 },
	{ 12, 4, 14, 6 },
	{ 3, 11, 1, 9 },
	{ 15, 7, 13, 5 },
};
} // namespace

double ToneCurve( double luma, double threshold )
{
	const double t = std::min( std::max( threshold, 0.001 ), 0.999 );
	const double v = std::min( std::max( luma, 0.0 ), 1.0 );
	return v < t ? 0.5 * v / t : 0.5 + 0.5 * ( v - t ) / ( 1.0 - t );
}

void DitherGrid( const std::vector< float >& luma, int columns, int rows, int mode, float threshold,
                 std::vector< uint8_t >& bits )
{
	const size_t n = static_cast< size_t >( columns ) * static_cast< size_t >( rows );
	bits.assign( n, 0 );
	if( luma.size() < n )
		return;
	const double tth = static_cast< double >( threshold );

	switch( mode )
	{
	case kDitherBayer:
		for( int r = 0; r < rows; ++r )
			for( int c = 0; c < columns; ++c )
			{
				const double level = ( static_cast< double >( kBayer[ r & 3 ][ c & 3 ] ) + 0.5 ) / 16.0;
				const size_t i     = static_cast< size_t >( r ) * columns + c;
				bits[ i ]          = ToneCurve( static_cast< double >( luma[ i ] ), tth ) >= level ? 1 : 0;
			}
		break;

	case kDitherFloyd:
	{
		// Two rows of error, in double so a long row cannot drift.
		std::vector< double > here( static_cast< size_t >( columns ) + 2, 0.0 ), below( static_cast< size_t >( columns ) + 2, 0.0 );
		for( int r = 0; r < rows; ++r )
		{
			const bool rightward = ( r & 1 ) == 0;
			std::fill( below.begin(), below.end(), 0.0 );
			for( int k = 0; k < columns; ++k )
			{
				const int c       = rightward ? k : columns - 1 - k;
				const int dir     = rightward ? 1 : -1;
				const size_t i    = static_cast< size_t >( r ) * columns + c;
				const double v    = ToneCurve( static_cast< double >( luma[ i ] ), tth ) + here[ static_cast< size_t >( c + 1 ) ];
				const uint8_t bit = v >= 0.5 ? 1 : 0;
				bits[ i ]         = bit;
				const double err  = v - static_cast< double >( bit );
				// Offsets by one so c - 1 and c + 1 are always in the arrays.
				here[ static_cast< size_t >( c + 1 + dir ) ] += err * 7.0 / 16.0;
				below[ static_cast< size_t >( c + 1 - dir ) ] += err * 3.0 / 16.0;
				below[ static_cast< size_t >( c + 1 ) ] += err * 5.0 / 16.0;
				below[ static_cast< size_t >( c + 1 + dir ) ] += err * 1.0 / 16.0;
			}
			std::swap( here, below );
		}
		break;
	}

	case kDitherThreshold:
	default:
		for( size_t i = 0; i < n; ++i )
			bits[ i ] = ToneCurve( static_cast< double >( luma[ i ] ), tth ) >= 0.5 ? 1 : 0;
		break;
	}
}

} // namespace flipdot
