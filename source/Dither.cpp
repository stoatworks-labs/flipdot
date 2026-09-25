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

void DitherGrid( const std::vector< float >& luma, int columns, int rows, int mode, float threshold,
                 std::vector< uint8_t >& bits )
{
	const size_t n = static_cast< size_t >( columns ) * static_cast< size_t >( rows );
	bits.assign( n, 0 );
	if( luma.size() < n )
		return;

	switch( mode )
	{
	case kDitherBayer:
		for( int r = 0; r < rows; ++r )
			for( int c = 0; c < columns; ++c )
			{
				const float t      = threshold + ( static_cast< float >( kBayer[ r & 3 ][ c & 3 ] ) + 0.5f ) / 16.0f - 0.5f;
				const size_t i     = static_cast< size_t >( r ) * columns + c;
				bits[ i ]          = luma[ i ] >= t ? 1 : 0;
			}
		break;

	case kDitherFloyd:
	{
		// Two rows of error, in double so a long row cannot drift.
		std::vector< double > here( static_cast< size_t >( columns ) + 2, 0.0 ), below( static_cast< size_t >( columns ) + 2, 0.0 );
		const double bias = 0.5 - static_cast< double >( threshold );
		for( int r = 0; r < rows; ++r )
		{
			const bool rightward = ( r & 1 ) == 0;
			std::fill( below.begin(), below.end(), 0.0 );
			for( int k = 0; k < columns; ++k )
			{
				const int c       = rightward ? k : columns - 1 - k;
				const int dir     = rightward ? 1 : -1;
				const size_t i    = static_cast< size_t >( r ) * columns + c;
				const double v    = static_cast< double >( luma[ i ] ) + bias + here[ static_cast< size_t >( c + 1 ) ];
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
			bits[ i ] = luma[ i ] >= threshold ? 1 : 0;
		break;
	}
}

} // namespace flipdot
