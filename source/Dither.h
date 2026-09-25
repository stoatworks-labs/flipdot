#pragma once

#include <cstdint>
#include <vector>

/**
    One bit per disc, from the mean luma of its cell.

    A flip-dot sign has two tones and nothing between, so the picture is
    thresholded or dithered at the dot pitch:

    Every mode first puts the luma through one tone curve: straight lines
    through (0, 0), (Threshold, 0.5) and (1, 1). So Threshold is the luma that
    lights half the discs, black stays black and white stays white whatever it
    is set to -- a bias would light a black clip with dots at a low Threshold,
    which on footage that is mostly black is the whole picture.

    - **Threshold**: on where the curve is at or above 0.5, i.e. where the
      luma is at or above Threshold.
    - **Bayer**: the 4 x 4 ordered matrix against the curve, so a flat grey of
      luma Threshold lights half the discs in the fixed pattern.
    - **Floyd-Steinberg**: error diffusion of the curve in serpentine scan
      order. Serial, which is why this runs on the CPU after a read-back of the
      grid of means rather than in a shader.

    Row 0 is the TOP of the sign in every array here.
*/
namespace flipdot
{
/// The curve through (0, 0), (threshold, 0.5) and (1, 1).
double ToneCurve( double luma, double threshold );

void DitherGrid( const std::vector< float >& luma, int columns, int rows, int mode, float threshold,
                 std::vector< uint8_t >& bits );
} // namespace flipdot
