#pragma once

/**
    The stated angular profile of one disc: how far it has swung, against
    time since its coil was pulsed.

    A disc is a flat plate on an axle through its diameter, with a magnet in
    it, sitting between two stops half a turn apart: black face out at one,
    fluorescent face out at the other. The model, in flip units u = t / Flip
    Time, with the angle theta measured from the stop the disc left:

    - **Driven.** The coil's field puts a constant torque on the magnet from
      the moment of the pulse, from rest: theta'' = 2 pi, so

            theta( u ) = pi u^2          for 0 <= u < 1,

      and the disc reaches the far stop at u = 1 exactly, at its fastest
      (theta' = 2 pi). Flip Time is therefore the time to first reach the far
      stop, which is what a driver's datasheet quotes.
    - **Damped, with a short rebound.** At the stop the disc bounces with
      restitution e (`Rebound`) and the latch magnet at that stop pulls it
      back twice as hard as the drive pushed it (theta'' = -4 pi toward the
      stop). The k-th rebound leaves at 2 pi e^k, lasts e^k flip units and
      rises pi e^(2k) / 2 off the stop:

            depth( s ) = 2 pi s ( e^k - s )   for s in [ 0, e^k ) of rebound k.

      Rebounds stop once the next would rise less than half a degree; from
      then on the disc is at the stop EXACTLY, so a settled sign is
      bit-identical frame to frame.
    - **A pulse toward the side a disc already shows** (Refresh All) drives it
      into its own stop. The model treats that as an impact at the arrival
      speed: the rebound train alone, from s = 0. With e = 0 it is invisible.

    `tools/fdtest --rotation` measures this off the rendered frames against
    its own numerical integration of the same torques, not against these
    closed forms.
*/
namespace flipdot::disc
{
constexpr double kPi = 3.14159265358979323846;

/// A rebound that would rise less than this does not happen: half a degree.
constexpr double kRestAngle = kPi / 360.0;

/// Radians below the stop, s flip units after an impact at arrival speed.
double ReboundDepth( double s, double restitution );

/// Flip units from an impact to the disc at rest on the stop.
double ReboundEnd( double restitution );

/// Radians from the stop the disc left, u flip units after the pulse.
/// `linear` is the negative control: constant speed, no rebound.
double SwingAngle( double u, double restitution, bool linear = false );

} // namespace flipdot::disc
