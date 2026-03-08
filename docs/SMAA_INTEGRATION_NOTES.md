# SMAA integration notes

## What I recommend

Use `shaders/` as the source of truth for buildable shader entry points, and keep `external/smaa/SMAA.hlsl` untouched as the upstream algorithm source.

This project’s compile flow (`compileslang.sh`) already compiles every `*.slang` file from `shaders/`, so adding wrappers there keeps hot-reload and pipeline rebuild behavior working without extra tooling.

## What was added

- `shaders/smaa_edge.slang`
- `shaders/smaa_weight.slang`
- `shaders/smaa_blend.slang`

Each wrapper:

- Uses the bindless descriptor model (`textures[]`, `samplers[]`, push constants).
- Includes `../external/smaa/SMAA.hlsl`.
- Exposes project entry points `vs_main` / `fs_main`.
- Computes `SMAA_RT_METRICS` dynamically from source texture dimensions.
- Uses `SMAA_PRESET_HIGH` and luma edge detection for SMAA 1x.

## Why not edit `external/smaa/SMAA.hlsl`

Keeping `external/smaa/SMAA.hlsl` unmodified is better for:

- Easier updates from upstream SMAA.
- Lower merge friction.
- Clear separation between third-party source and engine-specific binding/runtime glue.

## Practical caveat

SMAA ideally uses both linear and point sampling in different places. Current wrappers route sampling through the pass sampler and use `Load` for point-style samples where needed. If you want stricter reference behavior, add a dedicated point-clamp sampler ID to the push constants and wire `SMAASamplePoint` to it.
