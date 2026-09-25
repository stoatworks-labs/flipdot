# Attributions

Flipdot is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

**Provisional hand copy (2026-09-25).** In a registered repo this file is generated —
the master lists live in the `stoatworks-backend` repo and are pushed out by
`scripts/sync-attributions.py`. Flipdot is not registered yet, so this was written by
hand in that file's shape; register the project and re-run the sync before the first
release.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The effect skeleton, the onset detector and the harness — Stoatworks splitflap

<https://github.com/stoatworks-labs/splitflap>  
Licence: MIT  
Copyright: Stoatworks Labs

The plugin's skeleton (the copy pass with a mip chain, the sixteen-tap cell mean, the clock, the update decision, the About block), the primed spectral-flux onset detector, and the harness's shape — the headless session, the cue script, `--pipe`, `--list`, `--names`, `--bench`, the negative-control runner, `sweep.py`, `verify.sh`, `mutate.sh` and `clips.sh` — are splitflap's, adapted. The driver, the discs and the dither are new here.

### PassBuffer, the sweep and CI — Stoatworks tinsel

<https://github.com/stoatworks-labs/tinsel>  
Licence: MIT  
Copyright: Stoatworks Labs

`PassBuffer` (the SDK's FFGLFBO with the colour-texture leak fixed), the dead-control sweep and the CI and release workflows follow tinsel, by way of splitflap.

### The software-renderer switch — Stoatworks repousse and stencil

<https://github.com/stoatworks-labs/repousse>  
Licence: MIT  
Copyright: Stoatworks Labs

`FDTEST_RENDERER=software` asks for Apple's software renderer by id, as repousse's harness first did and stencil's carries.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl.

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

### The electromagnetic flip-dot sign

Bistable discs, black on one side and fluorescent on the other, swung by a coil pulse and held by a magnet; driver boards that scan a column at a time and pulse only what changes; the dead dot and the slow dot every old bus sign has. Implemented from that description; no manufacturer's firmware, datasheet or artwork was used.

## Standards and published specifications

What the implementation is measured against.

- **Robert W. Floyd and Louis Steinberg, "An Adaptive Algorithm for Spatial Greyscale" (Proceedings of the SID 17, 1976)** — Error diffusion with the 7/16, 3/16, 5/16, 1/16 weights, here in serpentine order.
- **Bryce E. Bayer, "An optimum method for two-level rendition of continuous-tone pictures" (IEEE ICC 1973)** — The 4 x 4 ordered-dither index matrix.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
