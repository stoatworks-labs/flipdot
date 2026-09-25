#pragma once

/**
    The three passes, as GLSL source.

    1. **copy**   picture size. Resolves MaxUV once and carries a mip chain,
                  so the means pass can read a disc's cell with a few fetches.
    2. **means**  grid size (Columns x Rows), R32F. One fragment per disc: the
                  mean luma of its cell, sixteen taps at a whole mip level
                  whose texel is at most an eighth of the pitch. Read back to
                  the CPU, where the dither (serial for Floyd-Steinberg) and
                  the driver and the discs live.
    3. **board**  output size. Each disc as a flat plate turned by its angle
                  about a vertical axle: black face or colour face, a rim
                  when edge-on, a hole in the black sign face behind it, and
                  Lambert shading from the Light direction with the hole's rim
                  shadowing the side away from it.

    Every shader is `#version 410 core`. Reserved words avoided as
    identifiers: patch sample input output filter common active half layout
    flat packed, and the rest of GLSL 4.10's reserved list (external,
    interface, fixed, long, short, public, static, row_major...). The Layout
    control reaches the shader as `OffsetRows`.

    Each shader is written as one or more adjacent raw strings, joined by the
    compiler: MSVC refuses a single literal past about 16 KB (C2026), and
    `tools/verify.sh`'s extraction joins them the same way.
*/
namespace flipdot
{

extern const char* const kVertexShader;
extern const char* const kCopyShader;
extern const char* const kMeansShader;
extern const char* const kBoardShader;

} // namespace flipdot
