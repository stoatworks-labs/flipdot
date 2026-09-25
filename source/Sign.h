#pragma once

#include <cstdint>
#include <vector>

/**
    The sign: every disc's state, and the driver board that scans them.

    This is all CPU and all double. A sign is at most 192 x 108 discs, the
    driver's timing is the claim the plugin is built on, and Resolume's clock
    (~499 million ms) cannot be carried in a float, so nothing here ever sees
    an absolute time: `Advance` is handed the frame's elapsed seconds and
    every time in the model is relative to the current frame.

    ## The driver

    It visits one line at a time -- a column, or a row with `Scan` -- at Scan
    Rate lines per second, in order from line 0. A pass is a sweep from line 0
    to the last; line L is visited L / Scan Rate seconds after the pass
    started, to the double, inside whatever frame that falls in. At each visit
    every disc of the line whose target differs from what the driver last
    wrote to it is pulsed (Only Changes), or every disc is (Refresh All).

    - **Continuous**: the driver never stops. It sweeps, wraps to line 0 and
      sweeps again, comparing each line against the live target as it
      reaches it.
    - **Interval, Onset, Manual**: an update latches the target and starts a
      pass. An update during a pass latches the new target at once (the lines
      not yet visited get it) and queues one more pass, which starts on the
      pass's own cadence when this one ends.

    ## A disc

    `side` is the stop it is at or swinging to; `commanded` is what the driver
    last wrote to it, which is what Only Changes compares against (a driver
    does not read its discs back). A pulse that finds a disc mid-swing toward
    the other side is queued and fires the instant it lands; a pulse on a disc
    in its rebound starts the new swing from the stop. A stuck disc takes the
    command and never moves. A weak coil (Late) swings 1.5 to 3 times slower.

    The state lives here, not in a GPU buffer, so a picture resize cannot touch
    it and a Columns or Rows change re-maps it: each new disc takes the old
    disc at the same place on the sign. `Angles()` is what the board shader
    draws, uploaded to a float texture every frame.
*/
namespace flipdot
{
class Sign
{
public:
	struct Settings
	{
		bool continuous     = true;
		bool scanRows       = false;
		double scanRate     = 240.0;///< lines per second
		bool onlyChanges    = true;
		double flipTime     = 0.04; ///< seconds to first reach the far stop
		double restitution  = 0.3;
	};

	/// Negative controls: each breaks one claim so a check can be seen to fail.
	struct Debug
	{
		bool noScan        = false;///< every line visited the instant of the update
		bool pulseAll      = false;///< Only Changes pulses every disc anyway
		bool ignoreStuck   = false;///< stuck discs obey their coils
		bool linearProfile = false;///< constant angular speed, no rebound
		double flipScale   = 1.0;  ///< the swing detuned without Flip Time knowing
		bool clearOnRegrid = false;///< a Columns or Rows change starts a blank sign
	};

	enum Motion : uint8_t
	{
		kRest  = 0,
		kSwing = 1,
		kKick  = 2
	};

	struct Disc
	{
		uint8_t side      = 0;///< 0 black face out, 1 colour face out: the stop it is at or heading for
		uint8_t commanded = 0;///< what the driver last wrote
		int8_t queued     = -1;///< a side to swing to the moment it lands, or -1
		uint8_t motion    = kRest;
		double age        = 0.0;///< seconds since the motion began
	};

	struct Traits
	{
		bool stuck        = false;
		uint8_t stuckSide = 0;
		double weak       = 1.0;///< flip time multiplier; 1 for a sound coil
	};

	/// Size the sign, re-mapping the state, and seed the stuck and late sets.
	/// The stuck set is exactly round( stuck x N ) discs, the first of a seeded
	/// order of all of them, so the count is the stated fraction and a larger
	/// fraction keeps every disc a smaller one had.
	void Configure( int columns, int rows, double stuck, double late );

	/// Every disc black, the driver idle. A fresh sign.
	void Reset();

	/// One frame. `dt` is the seconds since the last frame; `fire` is an
	/// update this frame (ignored when continuous); `target` is one byte per
	/// disc, row 0 the top, and is read only when a pass needs it.
	void Advance( double dt, bool fire, const std::vector< uint8_t >& target, const Settings& settings );

	int Columns() const
	{
		return mColumns;
	}
	int Rows() const
	{
		return mRows;
	}

	/// Radians of every disc from the black stop: 0 black face out, pi colour
	/// face out. Row 0 is the top.
	const std::vector< float >& Angles() const
	{
		return mAngles;
	}

	const Disc& DiscAt( int column, int row ) const
	{
		return mDiscs[ index( column, row ) ];
	}
	const Traits& TraitsAt( int column, int row ) const
	{
		return mTraits[ index( column, row ) ];
	}
	int StuckCount() const
	{
		return mStuckCount;
	}
	bool PassActive() const
	{
		return mActive;
	}

	Debug debug;

private:
	size_t index( int column, int row ) const
	{
		return static_cast< size_t >( row ) * static_cast< size_t >( mColumns ) + static_cast< size_t >( column );
	}
	void seed( double stuck, double late );
	void evolve( Disc& d, const Traits& t, double dt, const Settings& s ) const;
	void pulse( Disc& d, const Traits& t, uint8_t desired, const Settings& s ) const;
	float angleOf( const Disc& d, const Traits& t, const Settings& s ) const;
	double swingSeconds( const Traits& t, const Settings& s ) const;

	int mColumns = 0, mRows = 0;
	double mStuck = -1.0, mLate = -1.0;
	int mStuckCount = 0;
	std::vector< Disc > mDiscs;
	std::vector< Traits > mTraits;
	std::vector< float > mAngles;

	// The driver.
	bool mActive   = false;
	bool mPending  = false;
	bool mWasContinuous = false;
	double mHead   = 0.0;///< lines, since this pass's line 0 was visited
	int mNext      = 0;  ///< the next line to visit
	std::vector< uint8_t > mLatched;
	std::vector< std::vector< double > > mVisits;///< per line, this frame: seconds before now, oldest first
};

} // namespace flipdot
