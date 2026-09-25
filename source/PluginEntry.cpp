/**
	The FF_EFFECT registration, and nothing else.

	**This file is listed directly in the MODULE target, not in the shared
	object library.** `CFFGLPluginInfo` registers itself from a file-scope
	constructor and nothing ever references it by name, so in a static archive
	the linker is entitled to drop the whole translation unit -- giving a bundle
	that loads, exports `plugMain`, and reports that it contains no plugins.

	    nm -gU Flipdot.bundle/Contents/MacOS/Flipdot | grep plugMain

	`FD01`: four characters, unique across the fleet.
*/
#include "Flipdot.h"

static CFFGLPluginInfo PluginInfo(
	PluginFactory< flipdot::FlipdotPlugin >,                 // Create method
	"FD01",                                                  // Plugin unique ID of maximum length 4
	"SW Flipdot",                                            // Plugin name
	2,                                                       // API major version number
	1,                                                       // API minor version number
	0,                                                       // Plugin major version number
	1,                                                       // Plugin minor version number
	FF_EFFECT,                                               // Plugin type
	"The picture on an electromagnetic flip-dot sign.\n\n"
	"Every dot is a disc, black on one side and fluorescent on the other, that a coil swings from one face to the "
	"other and that stays where it is with no power. The driver scans the sign a column at a time and pulses only "
	"the discs that need to change, so a new picture wipes across the sign at the scan rate, each disc swinging "
	"over with its edge-on moment and a short rebound. One bit per dot, thresholded or dithered; a few dots stuck "
	"and a few late.\n\n"
	"Scan continuously, or update on an interval, on an audio onset, or by hand with Update Now.",// Plugin description
	"Flipdot FFGL effect"                                    // About
);

extern "C" const char* FlipdotBuildStamp()
{
	return "flipdot " FLIPDOT_VERSION " built " __DATE__ " " __TIME__;
}
