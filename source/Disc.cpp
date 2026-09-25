#include "Disc.h"

#include <cmath>

namespace flipdot::disc
{
namespace
{
/// Rebound k rises pi e^(2k) / 2; the train is cut at the first that would
/// rise less than kRestAngle. Forty is far past where any e <= 0.6 stops.
constexpr int kMaxRebounds = 40;

bool reboundHappens( double ek )
{
	return kPi * ek * ek * 0.5 >= kRestAngle;
}
} // namespace

double ReboundDepth( double s, double restitution )
{
	if( !( restitution > 0.0 ) || s < 0.0 )
		return 0.0;
	double ek = 1.0;
	for( int k = 1; k <= kMaxRebounds; ++k )
	{
		ek *= restitution;
		if( !reboundHappens( ek ) )
			return 0.0;
		if( s < ek )
			return 2.0 * kPi * s * ( ek - s );
		s -= ek;
	}
	return 0.0;
}

double ReboundEnd( double restitution )
{
	if( !( restitution > 0.0 ) )
		return 0.0;
	double end = 0.0, ek = 1.0;
	for( int k = 1; k <= kMaxRebounds; ++k )
	{
		ek *= restitution;
		if( !reboundHappens( ek ) )
			break;
		end += ek;
	}
	return end;
}

double SwingAngle( double u, double restitution, bool linear )
{
	if( u <= 0.0 )
		return 0.0;
	if( linear )
		return u < 1.0 ? kPi * u : kPi;
	if( u < 1.0 )
		return kPi * u * u;
	return kPi - ReboundDepth( u - 1.0, restitution );
}

} // namespace flipdot::disc
