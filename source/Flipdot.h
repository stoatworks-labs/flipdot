#pragma once

#include "Clock.h"
#include "Controls.h"
#include "Onset.h"
#include "PassBuffer.h"
#include "Sign.h"

#include <FFGLSDK.h>

#include <string>
#include <vector>

/**
    Flipdot -- the picture on an electromagnetic flip-dot sign, for Resolume.

    A flip-dot sign is a grid of small discs, black on one side and
    fluorescent on the other, each with a magnet in it between the poles of a
    coil. A pulse one way swings a disc to its colour face, the other way to
    black, and **it stays where it is with no power**. The driver board scans:
    it pulses one column at a time, so a whole-sign change sweeps across the
    sign at the scan rate, and it only pulses the discs that need to change.
    Everything about the look falls out of that hardware: the wipe, the swing
    and its edge-on moment, the one bit per dot, the stuck and the late discs,
    and a sign that holds its picture when nothing is driving it.

    Three passes (Shaders.h) and a CPU model between them (Sign.h): the clip
    with a mip chain; the mean luma of every disc's cell, read back; the
    dither, the driver and the discs in double on the CPU; the board, drawn
    from one float angle per disc.

    See AGENTS.md for the traps and what is verified.
*/
namespace flipdot
{
class FlipdotPlugin : public CFFGLPlugin
{
public:
	FlipdotPlugin();

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	char* GetTextParameter( unsigned int index ) override;
	FFResult SetTime( double time ) override;

	// -- Test hooks. The harness drives the real plugin class, so its knobs
	// -- are members rather than a second copy of anything.

	/// Negative controls: each breaks one claim so a check can be seen to fail.
	struct Debug
	{
		Sign::Debug sign;
		bool clearOnResize = false;///< the photofinish bug: the sign wiped on a picture resize
		bool noPrime       = false;///< the onset detector unprimed on frame one
		bool floatClock    = false;///< the frame delta taken between two floats of the host's clock
	};
	void SetDebugForTest( const Debug& debug );

	/// The offline harness declares its clock unit rather than letting the
	/// plugin infer one.
	void ForceSecondsClock()
	{
		mClock.ForceSeconds();
	}
	void ForceMillisecondsClock()
	{
		mClock.ForceMilliseconds();
	}

	/// Whether the onset detector fired on the last frame, for `--prime`.
	bool OnsetFiredForTest() const
	{
		return mOnsetFired;
	}

	/// The sign, for the seeded stuck set `--stuck` compares the picture with.
	const Sign& SignForTest() const
	{
		return mSign;
	}

private:
	bool uploadAngles();
	void decideUpdate( double now );

	ffglex::FFGLShader mCopyShader;
	ffglex::FFGLShader mMeansShader;
	ffglex::FFGLShader mBoardShader;
	ffglex::FFGLScreenQuad mQuad;

	PassBuffer mCopy; ///< the clip, ours, mipmapped
	PassBuffer mMeans;///< Columns x Rows, R32F: a disc's mean luma

	GLuint mAngleTexture = 0;
	int mAngleColumns = 0, mAngleRows = 0;
	std::vector< float > mTurn;///< cos and sin of every disc's angle, from double

	Sign mSign;
	std::vector< float > mLuma;
	std::vector< uint8_t > mTarget;
	Debug mDebug;

	Clock mClock;
	double mHostTime    = 0.0;
	bool mHostTimeSeen  = false;
	double mLastSeconds = -1.0;
	bool mFirstFrame    = true;

	Onset mOnset;
	bool mOnsetFired = false;

	// Update decisions.
	bool mFireThisFrame = false;
	bool mUpdatePending = false;///< the Update Now event
	double mNextTick    = -1.0; ///< Interval mode
	int mLastUpdateMode = -1;

	int mPictureWidth = 0, mPictureHeight = 0;
	bool mAudioSeen = false;

	float mParams[ PT_COUNT_ ] = {};
	std::string mAboutText;
};

} // namespace flipdot
