#pragma once

#include <cstdint>
#include <vector>

/**
    One bit per disc, from the mean luma of its cell.

    A flip-dot sign has two tones and nothing between, so the picture is
    thresholded or dithered at the dot pitch:

    - **Threshold**: on where the luma is at or above Threshold.
    - **Bayer**: the 4 x 4 ordered matrix, centred on Threshold, so a flat
      grey of luma Threshold lights half the discs in the fixed pattern.
    - **Floyd-Steinberg**: error diffusion in serpentine scan order, with the
      picture biased by 0.5 - Threshold. Serial, which is why this runs on the
      CPU after a read-back of the grid of means rather than in a shader.

    Row 0 is the TOP of the sign in every array here.
*/
namespace flipdot
{
void DitherGrid( const std::vector< float >& luma, int columns, int rows, int mode, float threshold,
                 std::vector< uint8_t >& bits );
} // namespace flipdot
