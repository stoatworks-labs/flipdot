#pragma once

#include <FFGLSDK.h>

// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

/**
    Every parameter, and what its host-side value means.

    ## The two units

    - **`FF_TYPE_STANDARD` is 0..1**, always. `SetParamInfo` clamps the default
      into that range before `SetParamRange` could widen it, so a standard
      parameter that means anything else is mapped here, in a named function,
      and nowhere else. The plugin and the harness both read the mapping from
      this one file.
    - **`FF_TYPE_INTEGER` holds a real integer** with a real range: Columns and
      Rows are counts, and the clamp does not apply.
    - **`FF_TYPE_OPTION` holds the element VALUE**, which for every dropdown
      here is its index. Its SDK range reads back 0..1 whatever the element
      count (the fleet trap), so `fdtest --list` prints the real range for the
      sweep, and `OptionIndex()` accepts either an index or a normalised 0..1.
    - **`FF_TYPE_BOOLEAN` is 0 or 1**, read as `> 0.5f`.
    - **`FF_TYPE_BUFFER`** holds the host's 64-bin spectrum. The host writes it.

    ## Order is load-bearing

    The host draws parameters in declaration order and `SetParamGroup`
    collapses *runs* of the same group name into one fold. The enum below is
    the inspector, top to bottom.

    ## Names

    FFGL truncates a parameter name at 16 characters, silently. `fdtest
    --names` lists any that are over, and any duplicate.
*/
namespace flipdot
{
enum ParamId : FFUInt32
{
	// -- Sign ----------------------------------------------------------------
	PT_COLUMNS,
	PT_ROWS,
	PT_LAYOUT,
	PT_DISC_SIZE,
	PT_COLOUR,

	// -- Driver --------------------------------------------------------------
	PT_SCAN,
	PT_SCAN_RATE,
	PT_UPDATE,
	PT_INTERVAL,
	PT_ONLY_CHANGES,
	PT_UPDATE_NOW,
	PT_AUDIO,

	// -- Discs ---------------------------------------------------------------
	PT_FLIP_TIME,
	PT_REBOUND,
	PT_STUCK,
	PT_LATE,

	// -- Look ----------------------------------------------------------------
	PT_DITHER,
	PT_THRESHOLD,
	PT_LIGHT,
	PT_MIX,

	// -- About ---------------------------------------------------------------
	// One text line and one button per link. Its size is decided by
	// StoatworksAbout.h at compile time, so Flipdot.cpp static_asserts this
	// run against `about::kParamCount`: four entries while there is no user
	// guide (text, Project page, Source on GitHub, Support the work).
	PT_ABOUT_TEXT,
	PT_ABOUT_BUTTON_1,
	PT_ABOUT_BUTTON_2,
	PT_ABOUT_BUTTON_3,

	PT_COUNT_
};

/// Spectrum bins in the Audio buffer parameter.
constexpr int kAudioBins = 64;

// -- Ranges of the integer parameters -----------------------------------------
constexpr int kColumnsMin = 4, kColumnsMax = 192, kColumnsDefault = 64;
constexpr int kRowsMin = 2, kRowsMax = 108, kRowsDefault = 36;

// -- Options ----------------------------------------------------------------
enum Layout
{
	kLayoutSquare = 0,
	kLayoutOffset = 1,
	kLayoutCount  = 2
};

enum Colour
{
	kColourYellow = 0,
	kColourGreen  = 1,
	kColourWhite  = 2,
	kColourCount  = 3
};

enum ScanOrder
{
	kScanColumns = 0,
	kScanRows    = 1,
	kScanCount   = 2
};

enum UpdateMode
{
	kUpdateContinuous = 0,
	kUpdateInterval   = 1,
	kUpdateOnset      = 2,
	kUpdateManual     = 3,
	kUpdateCount      = 4
};

enum DitherMode
{
	kDitherThreshold = 0,
	kDitherBayer     = 1,
	kDitherFloyd     = 2,
	kDitherCount     = 3
};

/// An option's stored value is its element index; a host or a script may
/// also hand over a normalised 0..1, which this folds back onto an index.
int OptionIndex( float value, int count );

// -- Standard (0..1) parameters, in engineering units ----------------------------

/// Disc diameter as a fraction of the pitch, 0.4..1.0, linear.
float DiscSizeFromParam( float value );

/// Lines (columns, or rows) the driver visits per second, 4..4000, geometric.
double ScanRateFromParam( float value );
float ScanRateToParam( double linesPerSecond );

/// Seconds between updates in Interval mode, 0.1..10, geometric.
double IntervalFromParam( float value );
float IntervalToParam( double seconds );

/// Seconds for a disc to swing from one stop to the other, 5 ms..1 s,
/// geometric. The time to FIRST reach the far stop; the rebound follows.
double FlipTimeFromParam( float value );
float FlipTimeToParam( double seconds );

/// Coefficient of restitution at the stop, 0..0.6, linear. 0 lands dead.
double ReboundFromParam( float value );
float ReboundToParam( double restitution );

/// Fraction of discs that never move, 0..0.2, linear.
double StuckFromParam( float value );
float StuckToParam( double fraction );

/// Fraction of discs on a weak coil, 0..0.5, linear.
double LateFromParam( float value );
float LateToParam( double fraction );

/// The light's azimuth in radians, 0 (from the viewer) to 80 degrees (grazing
/// from the left edge of the sign), linear.
float LightAngleFromParam( float value );

} // namespace flipdot
