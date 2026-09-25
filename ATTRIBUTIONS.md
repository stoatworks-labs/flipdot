# Attributions

Flipdot is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### Effect skeleton, onset detector and harness — Stoatworks splitflap

<https://github.com/stoatworks-labs/splitflap>  
Licence: MIT  
Copyright: Stoatworks Labs

The plugin's skeleton (the copy pass with a mip chain, the sixteen-tap cell mean, the update decision), the primed spectral-flux onset detector, and the harness's shape: the headless session, the cue script, --pipe (SIGPIPE ignored, a closed stdout exits 1), --list, --names, --bench, the negative-control runner, sweep.py, verify.sh, mutate.sh and clips.sh. The driver, the discs and the dither are new here.

### Diag logger and host Clock — Stoatworks graticule

<https://github.com/stoatworks-labs/graticule>  
Licence: MIT  
Copyright: Stoatworks Labs

source/Diag.* and source/Clock.* (the clock that settles the unit the host's SetTime arrives in), carried from graticule by way of splitflap.

### PassBuffer, the sweep and CI — Stoatworks tinsel

<https://github.com/stoatworks-labs/tinsel>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.*, the SDK's FFGLFBO with the colour-texture leak fixed, the dead-control sweep and the CI and release workflows follow tinsel, by way of splitflap.

### The software-renderer switch — Stoatworks repousse and stencil

<https://github.com/stoatworks-labs/repousse>  
Licence: MIT  
Copyright: Stoatworks Labs

FDTEST_RENDERER=software asks for Apple's software renderer by id, as repousse's harness first did and stencil's carries.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### The flip-dot sign

A bistable disc with a magnet between the poles of a coil, swung by a current pulse and held with no power, and a driver board that scans a line at a time and pulses only what changes, is the standard account of electromagnetic flip-dot displays as their makers describe them. No manufacturer's firmware, artwork, typeface or sound was used; the swing's torque law, the rebound's restitution, the timings and every constant are this repo's own, stated in source/Disc.h and source/Sign.h as assumptions, not measurements.

### The dithers

Ordered dithering by the recursive index matrix is B. E. Bayer's ("An optimum method for two-level rendition of continuous-tone pictures", IEEE ICC, 1973); error diffusion with the 7, 3, 5, 1 weights is R. W. Floyd and L. Steinberg's ("An adaptive algorithm for spatial greyscale", Proc. SID 17, 1976). The algorithms are the published ones; the tone curve and the code are this repo's.

## Standards and published specifications

What the implementation is measured against.

- **ITU-R BT.709** — the luma weights (0.2126, 0.7152, 0.0722) each disc's mean is read from the clip with.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
