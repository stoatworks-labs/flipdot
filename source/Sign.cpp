#include "Sign.h"

#include "Disc.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace flipdot
{
namespace
{
/// The PCG output permutation, exact in 32-bit integers.
uint32_t hashInt( uint32_t x )
{
	x = x * 747796405u + 2891336453u;
	x = ( ( x >> ( ( x >> 28u ) + 4u ) ) ^ x ) * 277803737u;
	return ( x >> 22u ) ^ x;
}

uint32_t discHash( int column, int row, uint32_t salt )
{
	return hashInt( hashInt( static_cast< uint32_t >( column ) * 2654435761u ^ salt ) + static_cast< uint32_t >( row ) * 40503u );
}

double hash01( uint32_t h )
{
	return static_cast< double >( h & 0x00ffffffu ) / 16777216.0;
}

constexpr uint32_t kStuckSalt = 0x57u;
constexpr uint32_t kSideSalt  = 0x5Du;
constexpr uint32_t kLateSalt  = 0x1Au;
constexpr uint32_t kWeakSalt  = 0xE7u;
} // namespace

//---------------------------------------------------------------------------
void Sign::Reset()
{
	for( Disc& d : mDiscs )
		d = Disc {};
	mActive  = false;
	mPending = false;
	mHead    = 0.0;
	mNext    = 0;
	std::fill( mLatched.begin(), mLatched.end(), 0 );
}

void Sign::Configure( int columns, int rows, double stuck, double late )
{
	columns = std::max( columns, 1 );
	rows    = std::max( rows, 1 );
	if( columns != mColumns || rows != mRows )
	{
		const size_t n = static_cast< size_t >( columns ) * static_cast< size_t >( rows );
		std::vector< Disc > discs( n );
		std::vector< uint8_t > latched( n, 0 );
		if( mColumns > 0 && mRows > 0 && !mDiscs.empty() && !debug.clearOnRegrid )
		{
			// Each new disc takes the old disc at the same place on the sign.
			for( int r = 0; r < rows; ++r )
				for( int c = 0; c < columns; ++c )
				{
					const int oc   = std::min( c * mColumns / columns, mColumns - 1 );
					const int orow = std::min( r * mRows / rows, mRows - 1 );
					const size_t i = static_cast< size_t >( r ) * columns + c;
					discs[ i ]     = mDiscs[ index( oc, orow ) ];
					if( !mLatched.empty() )
						latched[ i ] = mLatched[ index( oc, orow ) ];
				}
		}
		mColumns = columns;
		mRows    = rows;
		mDiscs   = std::move( discs );
		mLatched = std::move( latched );
		mAngles.assign( n, 0.0f );
		mStuck = -1.0;//reseed
	}
	if( stuck != mStuck || late != mLate )
		seed( stuck, late );
}

void Sign::seed( double stuck, double late )
{
	const size_t n = mDiscs.size();
	std::vector< Traits > traits( n );

	// A seeded order of every disc, and the first round( f x N ) of it. The
	// count is then exactly the stated fraction of the sign, and raising the
	// fraction only ever adds discs.
	auto firstOf = [ & ]( uint32_t salt, double fraction ) {
		std::vector< uint32_t > order( n );
		std::iota( order.begin(), order.end(), 0u );
		std::vector< uint32_t > key( n );
		for( size_t i = 0; i < n; ++i )
			key[ i ] = discHash( static_cast< int >( i % mColumns ), static_cast< int >( i / mColumns ), salt );
		std::sort( order.begin(), order.end(), [ & ]( uint32_t a, uint32_t b ) { return key[ a ] != key[ b ] ? key[ a ] < key[ b ] : a < b; } );
		const size_t count = static_cast< size_t >( std::llround( std::min( std::max( fraction, 0.0 ), 1.0 ) * static_cast< double >( n ) ) );
		order.resize( std::min( count, n ) );
		return order;
	};

	for( uint32_t i : firstOf( kStuckSalt, stuck ) )
	{
		traits[ i ].stuck     = true;
		traits[ i ].stuckSide = static_cast< uint8_t >( discHash( static_cast< int >( i % mColumns ), static_cast< int >( i / mColumns ), kSideSalt ) >> 31u );
	}
	for( uint32_t i : firstOf( kLateSalt, late ) )
		traits[ i ].weak = 1.5 + 1.5 * hash01( discHash( static_cast< int >( i % mColumns ), static_cast< int >( i / mColumns ), kWeakSalt ) );

	// A disc freed from being stuck is where it was jammed, at rest. The
	// driver's memory is left alone: it never knew.
	if( traits.size() == mTraits.size() )
		for( size_t i = 0; i < n; ++i )
			if( mTraits[ i ].stuck && !traits[ i ].stuck )
			{
				mDiscs[ i ].side   = mTraits[ i ].stuckSide;
				mDiscs[ i ].motion = kRest;
				mDiscs[ i ].queued = -1;
				mDiscs[ i ].age    = 0.0;
			}

	mStuckCount = 0;
	for( const Traits& t : traits )
		mStuckCount += t.stuck ? 1 : 0;
	mTraits = std::move( traits );
	mStuck  = stuck;
	mLate   = late;
}

//---------------------------------------------------------------------------
double Sign::swingSeconds( const Traits& t, const Settings& s ) const
{
	return std::max( s.flipTime * t.weak * debug.flipScale, 1.0e-6 );
}

void Sign::evolve( Disc& d, const Traits& t, double dt, const Settings& s ) const
{
	if( d.motion == kRest )
		return;
	d.age += std::max( dt, 0.0 );
	const double T   = swingSeconds( t, s );
	const double end = debug.linearProfile ? 0.0 : disc::ReboundEnd( s.restitution );
	for( int guard = 0; guard < 16; ++guard )
	{
		if( d.motion == kSwing )
		{
			if( d.age >= T && d.queued >= 0 )
			{
				if( static_cast< uint8_t >( d.queued ) != d.side )
				{
					// Landed with a pulse waiting: swing back from the stop, the
					// instant it arrived.
					d.age -= T;
					d.side   = static_cast< uint8_t >( d.queued );
					d.queued = -1;
					continue;
				}
				d.queued = -1;
			}
			if( d.age >= T * ( 1.0 + end ) )
			{
				d.motion = kRest;
				d.age    = 0.0;
			}
		}
		else if( d.motion == kKick )
		{
			if( d.age >= T * end )
			{
				d.motion = kRest;
				d.age    = 0.0;
			}
		}
		break;
	}
}

void Sign::pulse( Disc& d, const Traits& t, uint8_t desired, const Settings& s ) const
{
	// Only Changes: the driver compares against what it last wrote, because it
	// cannot read a disc back.
	if( s.onlyChanges && !debug.pulseAll && desired == d.commanded )
		return;
	d.commanded = desired;
	if( t.stuck && !debug.ignoreStuck )
		return;

	const double T = swingSeconds( t, s );
	if( d.motion == kSwing && d.age < T )
	{
		// Mid-swing: the coil cannot turn it round in the air. Remember the
		// command for the moment it lands.
		d.queued = desired != d.side ? static_cast< int8_t >( desired ) : static_cast< int8_t >( -1 );
		return;
	}
	if( desired != d.side )
	{
		d.side   = desired;
		d.motion = kSwing;
		d.age    = 0.0;
		d.queued = -1;
		return;
	}
	// Toward the side it already shows: driven into its own stop.
	if( s.restitution > 0.0 && !debug.linearProfile )
	{
		d.motion = kKick;
		d.age    = 0.0;
		d.queued = -1;
	}
}

float Sign::angleOf( const Disc& d, const Traits& t, const Settings& s ) const
{
	if( t.stuck && !debug.ignoreStuck )
		return t.stuckSide ? static_cast< float >( disc::kPi ) : 0.0f;
	const double T = swingSeconds( t, s );
	double phi     = d.side ? disc::kPi : 0.0;
	if( d.motion == kSwing )
	{
		const double theta = disc::SwingAngle( d.age / T, s.restitution, debug.linearProfile );
		phi                = d.side ? theta : disc::kPi - theta;
	}
	else if( d.motion == kKick )
	{
		const double depth = disc::ReboundDepth( d.age / T, s.restitution );
		phi                = d.side ? disc::kPi - depth : depth;
	}
	return static_cast< float >( phi );
}

//---------------------------------------------------------------------------
void Sign::Advance( double dt, bool fire, const std::vector< uint8_t >& target, const Settings& s )
{
	const int lines = s.scanRows ? mRows : mColumns;
	const size_t n  = mDiscs.size();
	if( lines <= 0 || n == 0 )
		return;
	dt = std::max( dt, 0.0 );
	const double rate = std::max( s.scanRate, 1.0e-6 );

	//The driver's pass.
	if( mNext > lines )
	{
		mNext = lines;
		mHead = std::min( mHead, static_cast< double >( lines ) );
	}
	const bool wasActive = mActive;
	if( s.continuous || fire )
	{
		if( target.size() == n )
			mLatched = target;
		if( mActive )
			mPending = true;
		else
		{
			mActive = true;
			mHead   = 0.0;
			mNext   = 0;
		}
	}
	if( !s.continuous && mWasContinuous )
		mPending = false;//the sweep under way finishes; no more follow it
	mWasContinuous = s.continuous;
	if( wasActive )
		mHead += dt * rate;

	mVisits.resize( static_cast< size_t >( lines ) );
	for( auto& v : mVisits )
		v.clear();

	if( debug.noScan )
	{
		// Negative control: the whole sign at once, the instant of the update.
		if( mActive )
		{
			for( auto& v : mVisits )
				v.push_back( 0.0 );
			if( !s.continuous )
			{
				mActive  = false;
				mPending = false;
			}
			mHead = 0.0;
			mNext = 0;
		}
	}
	else
	{
		// Line L of this pass is visited when the head reaches it, L / rate
		// seconds after the pass began: (head - L) / rate seconds before now.
		const int limit = 64 * lines + 64;
		for( int guard = 0; mActive && guard < limit; ++guard )
		{
			if( mNext >= lines )
			{
				if( mPending || s.continuous )
				{
					mPending = s.continuous;
					mHead -= static_cast< double >( mNext );
					mNext = 0;
				}
				else
				{
					mActive = false;
					mHead   = 0.0;
					mNext   = 0;
				}
				continue;
			}
			if( static_cast< double >( mNext ) > mHead )
				break;
			mVisits[ static_cast< size_t >( mNext ) ].push_back( ( mHead - static_cast< double >( mNext ) ) / rate );
			++mNext;
		}
	}

	//Every disc: from the last frame to each visit of its line, the pulse, and
	//on to now.
	for( int r = 0; r < mRows; ++r )
		for( int c = 0; c < mColumns; ++c )
		{
			const size_t i  = index( c, r );
			Disc& d         = mDiscs[ i ];
			const Traits& t = mTraits[ i ];
			double at       = -dt;//seconds, relative to now, of the state held in d
			for( double ago : mVisits[ static_cast< size_t >( s.scanRows ? r : c ) ] )
			{
				const double when = -ago;
				evolve( d, t, when - at, s );
				at = std::max( at, when );
				pulse( d, t, mLatched[ i ], s );
			}
			evolve( d, t, -at, s );
			mAngles[ i ] = angleOf( d, t, s );
		}
}

} // namespace flipdot
