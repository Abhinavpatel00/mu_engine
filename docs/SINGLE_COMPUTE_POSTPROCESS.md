# Single Compute Pass Post-Processing: Full Analysis

## TL;DR Verdict

**Almost all of them — YES, in a single compute dispatch.** A few require caveats or workarounds. Two are fundamentally incompatible with a pure single-pass approach (volumetric lighting and FXAA-like smoothing) unless you accept approximations.

Below is an exhaustive breakdown of every effect, why it can or cannot fold into one pass, and the exact mechanics of how to do it.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Per-Effect Analysis](#per-effect-analysis)
   - [Tone Mapping](#1-tone-mapping-reinhard-aces-filmic)
   - [Gamma Correction](#2-gamma-correction)
   - [Color Grading (LUT or Matrix)](#3-color-grading-lut-or-matrix)
   - [Exposure](#4-exposure)
   - [White Balance](#5-white-balance)
   - [Contrast / Saturation / Lift-Gamma-Gain](#6-contrast--saturation--lift-gamma-gain)
   - [Vignette](#7-vignette)
   - [Film Grain (Hash-Based)](#8-film-grain-hash-based)
   - [Simple Sharpen (Unsharp Mask 3×3)](#9-simple-sharpen-unsharp-mask-3×3)
   - [Chromatic Aberration](#10-chromatic-aberration-screen-space-uv-offset)
   - [Dithering](#11-dithering)
   - [Volumetric Lighting](#12-volumetric-lighting)
   - [Lens Distortion](#13-lens-distortion)
   - [Sepia / Stylized Grading](#14-sepia--stylized-grading)
   - [Depth Visualization](#15-depth-visualization)
   - [FXAA-Like Edge Smoothing](#16-fxaa-like-edge-smoothing)
3. [Dependency Graph & Ordering](#dependency-graph--ordering)
4. [The Neighbor-Sampling Problem](#the-neighbor-sampling-problem)
5. [Practical Implementation](#practical-implementation)
6. [Push Constant Layout](#push-constant-layout)
7. [Full Shader Skeleton](#full-shader-skeleton)
8. [What Needs a Separate Pass and Why](#what-needs-a-separate-pass-and-why)
9. [Summary Table](#summary-table)

---

## Architecture Overview

A compute post-process pass works like this:

```
[HDR Color Image (storage/sampled)] --> Compute Shader --> [Output Image (storage)]
```

Each thread maps to one output pixel via `gl_GlobalInvocationID.xy`. The shader reads from the source image, applies a chain of transformations, and writes the final color to the output (or writes back in-place if you use a single image with appropriate barriers).

### What makes an effect "single-pass compatible"?

An effect is single-pass compatible if computing the output for pixel `(x, y)` requires **only**:

1. **The value of that same pixel** in the source image — trivially compatible.
2. **The values of a small, fixed neighborhood** of pixels in the **unmodified** source image — compatible, but requires reading from a read-only source (you cannot read-modify-write in place for these).
3. **Global uniform data** (exposure value, color matrix, LUT, etc.) — trivially compatible.
4. **Screen-space UV of the current pixel** — trivially compatible.

An effect is **NOT** single-pass compatible if:

- It requires the **results of another effect applied to neighbor pixels** (data dependency chain across pixels).
- It requires a **multi-pass gather** (e.g., large-radius blur, hierarchical operations).
- It requires **scene geometry information not available in the color buffer** (e.g., raymarching into the depth buffer for volumetrics).

---

## Per-Effect Analysis

### 1. Tone Mapping (Reinhard, ACES, Filmic)

**Single-pass compatible: YES — trivially.**

Tone mapping is a **per-pixel color transform**. It reads one pixel, applies a curve, outputs one pixel. Zero neighbor dependencies.

#### How it works in the pass

```hlsl
float3 tonemap_reinhard(float3 color) {
    return color / (1.0 + color);
}

float3 tonemap_aces(float3 color) {
    // Narkowicz ACES fit
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float3 tonemap_filmic(float3 color) {
    // Hejl-Burgess-Dawson filmic
    float3 x = max(float3(0), color - 0.004);
    return (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
}
```

**Order in chain:** Must happen **after** exposure is applied (exposure adjusts the HDR range, tone mapping compresses it). Must happen **before** gamma correction (tone mapping outputs linear, gamma converts to sRGB).

---

### 2. Gamma Correction

**Single-pass compatible: YES — trivially.**

Per-pixel power function. No neighbor dependencies.

```hlsl
float3 gamma_correct(float3 color, float gamma) {
    return pow(max(color, 0.0), float3(1.0 / gamma));
}
```

If outputting to an sRGB swapchain, the hardware does this for free via the `VK_FORMAT_*_SRGB` format and you can skip it. If outputting to `UNORM`, apply it in the shader.

**Order in chain:** **Last** color transform before output (after tone mapping and all grading).

---

### 3. Color Grading (LUT or Matrix)

**Single-pass compatible: YES.**

#### Matrix grading
Pure per-pixel `mat3 * color` multiply:

```hlsl
float3 color_grade_matrix(float3 color, float3x3 grading_matrix) {
    return mul(grading_matrix, color);
}
```

#### LUT grading
Uses a 3D LUT texture (typically 32³ or 64³ stored as a 2D strip). Still per-pixel — you sample the LUT at coordinates derived from the pixel's RGB:

```hlsl
float3 color_grade_lut(float3 color, Texture3D lut, SamplerState lut_sampler) {
    // color should be in [0,1] (post-tonemap)
    float3 lut_coord = color * ((LUT_SIZE - 1.0) / LUT_SIZE) + 0.5 / LUT_SIZE;
    return lut.SampleLevel(lut_sampler, lut_coord, 0).rgb;
}
```

The LUT is bound as a sampled image — no neighbor dependency on the color buffer.

**Order in chain:** After tone mapping (LUT expects [0, 1] input), before gamma.

---

### 4. Exposure

**Single-pass compatible: YES — trivially.**

Per-pixel multiply:

```hlsl
float3 apply_exposure(float3 color, float exposure) {
    return color * exposure; // exposure = pow(2, ev)
}
```

If you have auto-exposure, the exposure value is computed in a **prior** pass (luminance histogram → average → EV). But the application of that value is just a multiply per pixel.

**Order in chain:** **First** — applied to the raw HDR color before any tone mapping.

---

### 5. White Balance

**Single-pass compatible: YES — trivially.**

White balance is a per-pixel color matrix or temperature/tint adjustment. Common approach:

```hlsl
float3 white_balance(float3 color, float temperature, float tint) {
    // Convert temperature to a color shift
    // Simplified: adjust along blue-yellow (temp) and green-magenta (tint) axes
    float t = temperature; // negative = cool, positive = warm
    float g = tint;

    // LMS-space approach (more physically accurate):
    // 1. RGB → LMS
    // 2. Scale L, M, S channels
    // 3. LMS → RGB
    // Or use a pre-computed 3x3 matrix from (temperature, tint)

    float3x3 wb_matrix = compute_wb_matrix(temperature, tint);
    return mul(wb_matrix, color);
}
```

**Order in chain:** Before tone mapping (operates on HDR linear data).

---

### 6. Contrast / Saturation / Lift-Gamma-Gain

**Single-pass compatible: YES — trivially.**

All three are per-pixel operations on the color value, no neighbor reads.

```hlsl
float3 apply_contrast(float3 color, float contrast) {
    float midpoint = 0.5; // or 0.18 for HDR (middle gray)
    return (color - midpoint) * contrast + midpoint;
}

float3 apply_saturation(float3 color, float saturation) {
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    return lerp(float3(luma), color, saturation);
}

float3 lift_gamma_gain(float3 color, float3 lift, float3 gamma, float3 gain) {
    // ASC CDL model (standard color decision list)
    color = color * gain + lift;
    color = max(color, 0.0);
    color = pow(color, 1.0 / gamma);
    return color;
}
```

**Order in chain:** Contrast and saturation after tone mapping (operate on [0,1] range). Lift-gamma-gain can go either in linear HDR or post-tonemap depending on artistic intent.

---

### 7. Vignette

**Single-pass compatible: YES — trivially.**

Per-pixel darkening based on distance from screen center:

```hlsl
float3 apply_vignette(float3 color, float2 uv, float intensity, float smoothness) {
    float2 center_offset = uv - 0.5;
    float dist = length(center_offset);
    float vignette = smoothstep(0.5, 0.5 - smoothness, dist * intensity);
    return color * vignette;
}
```

Only needs the pixel's own UV coordinate. No neighbor reads.

**Order in chain:** After tone mapping and grading, before gamma.

---

### 8. Film Grain (Hash-Based)

**Single-pass compatible: YES — trivially.**

Hash-based grain uses the pixel coordinate and a time/frame uniform to generate noise. No texture reads, no neighbor dependencies:

```hlsl
float hash_grain(float2 uv, float time) {
    // Classic hash
    float3 p3 = frac(float3(uv.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33 + time);
    return frac((p3.x + p3.y) * p3.z);
}

float3 apply_film_grain(float3 color, float2 uv, float time, float grain_intensity) {
    float noise = hash_grain(uv, time);
    // Grain more visible in midtones (perceptual weighting)
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    float grain_weight = 1.0 - abs(luma * 2.0 - 1.0); // peaks at mid-gray
    color += (noise - 0.5) * grain_intensity * grain_weight;
    return color;
}
```

**Order in chain:** After grading, before or after gamma (after gamma = more visible grain in darks, which is more filmic).

---

### 9. Simple Sharpen (Unsharp Mask, 3×3)

**Single-pass compatible: YES — with a caveat.**

#### The caveat: neighbor sampling from the unmodified source

Sharpening needs to read the center pixel plus its neighbors (a 3×3 kernel). This is fine **as long as you read from the original, unmodified source image**. The output must go to a **different** image (or you accept the write-after-read hazard by ensuring workgroup-level isolation).

```hlsl
float3 apply_sharpen(Texture2D src, int2 pixel_coord, float sharpen_strength) {
    float3 center = src.Load(int3(pixel_coord, 0)).rgb;

    float3 neighbors = float3(0);
    neighbors += src.Load(int3(pixel_coord + int2(-1,  0), 0)).rgb;
    neighbors += src.Load(int3(pixel_coord + int2( 1,  0), 0)).rgb;
    neighbors += src.Load(int3(pixel_coord + int2( 0, -1), 0)).rgb;
    neighbors += src.Load(int3(pixel_coord + int2( 0,  1), 0)).rgb;
    neighbors *= 0.25;

    // Unsharp mask: center + (center - blur) * strength
    float3 sharpened = center + (center - neighbors) * sharpen_strength;
    return max(sharpened, 0.0);
}
```

#### Why this still works in a single pass

You read the **raw HDR source** from a read-only `Texture2D` / `SamplerState`. You write the result to a separate `RWTexture2D`. The sharpening reads neighbors of the **source** image, not the output image, so there is no data hazard.

**Critical ordering rule:** Sharpening must be applied to the source **before** per-pixel transforms are applied. The pipeline becomes:

1. Read neighbor samples from source (for sharpen, chromatic aberration).
2. Compute the sharpened / aberrated color.
3. Apply all per-pixel transforms (exposure → white balance → tone map → grade → etc.) to that computed color.
4. Write output.

This works perfectly in a single dispatch because all neighbor reads come from the unmodified source.

**Order in chain:** Logically first (reads raw source neighbors), then all per-pixel operations follow.

---

### 10. Chromatic Aberration (Screen-Space UV Offset)

**Single-pass compatible: YES — with the same caveat as sharpening.**

Chromatic aberration reads R, G, B channels at slightly different UV coordinates:

```hlsl
float3 apply_chromatic_aberration(Texture2D src, SamplerState samp,
                                   float2 uv, float2 resolution, float ca_strength) {
    float2 center_offset = uv - 0.5;
    float2 offset = center_offset * ca_strength / resolution;

    float r = src.SampleLevel(samp, uv + offset, 0).r;
    float g = src.SampleLevel(samp, uv, 0).g;
    float b = src.SampleLevel(samp, uv - offset, 0).b;

    return float3(r, g, b);
}
```

This samples the **source** image at offset UVs — reads from unmodified data, no hazard. Same principle as sharpening: all source-neighbor reads happen before per-pixel transforms.

**Order in chain:** Same stage as sharpening — early in the chain, reading from unmodified source.

---

### 11. Dithering

**Single-pass compatible: YES — trivially.**

Dithering adds a small noise offset to break color banding. Pure per-pixel:

```hlsl
float3 apply_dithering(float3 color, float2 pixel_coord) {
    // Bayer 8x8 or interleaved gradient noise
    float dither = interleaved_gradient_noise(pixel_coord) / 255.0;
    return color + dither - 0.5 / 255.0;
}

float interleaved_gradient_noise(float2 pixel_coord) {
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(pixel_coord, magic.xy)));
}
```

**Order in chain:** Absolute last step, after gamma correction, right before writing output. Dithering operates in the output color space (sRGB / 8-bit).

---

### 12. Volumetric Lighting

**Single-pass compatible: NO — fundamentally incompatible.**

#### Why it cannot be a single per-pixel pass

Volumetric lighting (god rays, light shafts, volumetric fog) requires one of:

- **Raymarching through the depth buffer** along each pixel's view ray, sampling shadow maps at each step. This is extremely expensive per pixel (16–64 samples along the ray) and each sample requires a shadow map lookup.
- **Bilateral upsampling** from a half-res or quarter-res volume texture.
- **Froxel-based** volumetric fog with a 3D texture that is built in a prior pass.

None of these are "just read the color buffer and transform it." They require:

1. **Depth buffer** access (not just the color buffer).
2. **Shadow map** access at arbitrary world positions.
3. **Light source data** (position, direction, color).
4. Per-pixel **raymarching loops** of variable length.

The result of volumetric lighting is an **additive fog/scatter color** that gets blended onto the scene. You *could* compute it in the same dispatch as the other post-processing effects, but:

- It would make the shader enormously expensive.
- It would require binding the depth buffer, shadow maps, and light data — bloating the descriptor set.
- It's typically done at reduced resolution and upsampled, which is impossible in a single full-res dispatch.

#### Recommended approach

Volumetric lighting is a **separate compute pass** (or multiple passes) that writes to a volumetric scattering texture. The post-process pass then **reads** that texture and applies it:

```hlsl
// In the single post-process pass, AFTER volumetric lighting was computed elsewhere:
float4 vol = volumetric_texture.SampleLevel(samp, uv, 0);
color = color * vol.a + vol.rgb; // multiplicative extinction + additive in-scatter
```

This final application step is trivially per-pixel and fits in the single pass. The computation of `volumetric_texture` does not.

---

### 13. Lens Distortion

**Single-pass compatible: YES — with the same caveat as chromatic aberration.**

Lens distortion remaps UVs with a radial distortion function, then samples the source at the distorted UV:

```hlsl
float2 apply_lens_distortion(float2 uv, float k1, float k2) {
    float2 centered = uv - 0.5;
    float r2 = dot(centered, centered);
    float distortion = 1.0 + k1 * r2 + k2 * r2 * r2;
    return centered * distortion + 0.5;
}

// Usage:
float2 distorted_uv = apply_lens_distortion(uv, k1, k2);
float3 color = src.SampleLevel(samp, distorted_uv, 0).rgb;
```

Reads from the **unmodified source** at a remapped UV. Same category as chromatic aberration.

**Interaction with chromatic aberration:** If you combine both, apply lens distortion first (remap the UV), then apply chromatic aberration offset to the distorted UV:

```hlsl
float2 dist_uv = apply_lens_distortion(uv, k1, k2);
float2 ca_offset = (dist_uv - 0.5) * ca_strength / resolution;
float r = src.SampleLevel(samp, dist_uv + ca_offset, 0).r;
float g = src.SampleLevel(samp, dist_uv, 0).g;
float b = src.SampleLevel(samp, dist_uv - ca_offset, 0).b;
```

**Order in chain:** Early — UV remapping happens before color transforms.

---

### 14. Sepia / Stylized Grading

**Single-pass compatible: YES — trivially.**

Per-pixel color matrix or lerp:

```hlsl
float3 apply_sepia(float3 color, float intensity) {
    float3 sepia;
    sepia.r = dot(color, float3(0.393, 0.769, 0.189));
    sepia.g = dot(color, float3(0.349, 0.686, 0.168));
    sepia.b = dot(color, float3(0.272, 0.534, 0.131));
    return lerp(color, sepia, intensity);
}

// Or arbitrary stylized grading via a color ramp:
float3 apply_stylized(float3 color, Texture2D color_ramp, SamplerState samp) {
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    float3 tinted = color_ramp.SampleLevel(samp, float2(luma, 0.5), 0).rgb;
    return tinted;
}
```

**Order in chain:** After tone mapping, alongside other color grading.

---

### 15. Depth Visualization

**Single-pass compatible: YES — trivially.**

Reads the depth buffer and maps it to a color. Per-pixel, no neighbor reads from the color buffer:

```hlsl
float3 visualize_depth(Texture2D depth_tex, int2 pixel_coord,
                       float near, float far) {
    float z = depth_tex.Load(int3(pixel_coord, 0)).r;

    // Linearize (reverse-Z assumed)
    float linear_depth = near * far / (far - z * (far - near));
    // or for reverse-Z: near * far / (near + z * (far - near))

    float normalized = saturate(linear_depth / far);

    // Color ramp
    return float3(normalized, normalized, normalized);
    // Or use a heat map: return heat_ramp(normalized);
}
```

Requires binding the depth buffer as a sampled image, which is free in a bindless setup.

**Order in chain:** This typically **replaces** the color output (debug view), so it's either-or with the normal post-process chain. You'd have a uniform flag to toggle it.

---

### 16. FXAA-Like Edge Smoothing

**Single-pass compatible: PARTIALLY — with significant compromises.**

#### Why it's problematic

FXAA (Fast Approximate Anti-Aliasing) works in **two stages**:

1. **Edge detection:** Compute luminance contrast between the current pixel and its neighbors.
2. **Edge-directed blending:** Sample along the detected edge direction to find edge endpoints, then blend the center pixel with neighbors to smooth the edge.

The problem: FXAA reads **luminance values of neighbors** and those neighbors should ideally be the **final post-processed, gamma-corrected** luminance. If you apply FXAA to the raw HDR source, tone mapping and gamma correction will shift the perceived edges.

#### The compromise: read source luma, accept slight inaccuracy

You *can* fold a simplified FXAA into the single pass if you:

1. Read the **source** image neighbors (raw HDR or pre-tonemapped).
2. Compute luminance contrast from those raw values.
3. Do the edge-directed blend on the source pixels.
4. Apply tone mapping / grading / gamma to the blended result.

This works because edges in the HDR image generally correspond to edges in the final image. The anti-aliasing quality won't be reference-perfect but is visually acceptable.

```hlsl
float3 simple_fxaa(Texture2D src, SamplerState samp, float2 uv, float2 texel_size) {
    float3 center = src.SampleLevel(samp, uv, 0).rgb;
    float luma_center = dot(center, float3(0.299, 0.587, 0.114));

    float luma_n = dot(src.SampleLevel(samp, uv + float2(0, -texel_size.y), 0).rgb,
                       float3(0.299, 0.587, 0.114));
    float luma_s = dot(src.SampleLevel(samp, uv + float2(0,  texel_size.y), 0).rgb,
                       float3(0.299, 0.587, 0.114));
    float luma_e = dot(src.SampleLevel(samp, uv + float2( texel_size.x, 0), 0).rgb,
                       float3(0.299, 0.587, 0.114));
    float luma_w = dot(src.SampleLevel(samp, uv + float2(-texel_size.x, 0), 0).rgb,
                       float3(0.299, 0.587, 0.114));

    float luma_range = max(max(luma_n, luma_s), max(luma_e, luma_w)) - 
                       min(min(luma_n, luma_s), min(luma_e, luma_w));

    if (luma_range < max(0.0312, luma_center * 0.125)) {
        return center; // No edge detected, skip AA
    }

    // Simplified: blend toward neighbor average
    float3 avg = (src.SampleLevel(samp, uv + float2(0, -texel_size.y), 0).rgb +
                  src.SampleLevel(samp, uv + float2(0,  texel_size.y), 0).rgb +
                  src.SampleLevel(samp, uv + float2( texel_size.x, 0), 0).rgb +
                  src.SampleLevel(samp, uv + float2(-texel_size.x, 0), 0).rgb) * 0.25;

    float blend = smoothstep(0.0, 1.0, luma_range / max(luma_center, 0.01));
    return lerp(center, avg, blend * 0.5);
}
```

#### Full-quality FXAA: requires a separate pass

Real FXAA 3.11 needs the **final luminance** (post-tonemap, post-gamma) stored in the alpha channel or computed from the final color. It then walks along edges for up to 12 steps. This requires a separate pass that runs **after** all post-processing is complete.

**Recommendation:** Either accept the simplified version in the single pass, or do a second pass for FXAA quality.

---

## Dependency Graph & Ordering

Here is the correct ordering of all effects within the single pass:

```
┌─────────────────────────────────────────────────────────┐
│  STAGE 1: UV REMAPPING & NEIGHBOR READS (from source)   │
│                                                         │
│  1. Lens distortion (remap UV)                          │
│  2. Chromatic aberration (offset UV per channel)        │
│  3. Sharpen (3×3 neighbor read from source)             │
│  4. FXAA-like smoothing (neighbor reads from source)    │
│                                                         │
│  → These ALL read from the UNMODIFIED source image.     │
│  → The source is bound as Texture2D (read-only).        │
│  → Output of this stage: one float3 color per pixel.    │
├─────────────────────────────────────────────────────────┤
│  STAGE 2: HDR PER-PIXEL TRANSFORMS                      │
│                                                         │
│  5. Exposure (multiply)                                 │
│  6. White balance (3×3 matrix in linear space)          │
│  7. Tone mapping (Reinhard / ACES / filmic)             │
│                                                         │
│  → Input is now in [0,1] after tone mapping.            │
├─────────────────────────────────────────────────────────┤
│  STAGE 3: LDR PER-PIXEL GRADING                         │
│                                                         │
│  8. Color grading (LUT or matrix)                       │
│  9. Contrast adjustment                                 │
│  10. Saturation adjustment                              │
│  11. Lift-gamma-gain                                    │
│  12. Sepia / stylized grading                           │
│  13. Depth visualization (replaces if debug mode)       │
│  14. Apply volumetric lighting (read pre-computed tex)  │
├─────────────────────────────────────────────────────────┤
│  STAGE 4: SPATIAL / NOISE / OUTPUT                      │
│                                                         │
│  15. Vignette                                           │
│  16. Film grain                                         │
│  17. Gamma correction (if not using sRGB surface)       │
│  18. Dithering (absolute last)                          │
│                                                         │
│  → Write final color to RWTexture2D output.             │
└─────────────────────────────────────────────────────────┘
```

---

## The Neighbor-Sampling Problem

This is the core concern when merging effects. Let's be precise:

### The rule

> **You can read neighbors from a read-only source. You cannot read neighbors from data you're concurrently writing.**

In Vulkan/compute terms:
- Bind the input HDR image as `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` or `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE` (read-only).
- Bind the output image as `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE` (write-only in practice).
- All neighbor reads (sharpen, CA, FXAA, lens distortion) sample from the **read-only input**.
- All writes go to the **output storage image**.

If input and output are different images (or different mip levels of the same image), there is **zero hazard**.

### What if I want to sharpen the tone-mapped image, not the raw HDR?

Then you need **two passes**:
1. Pass 1: All per-pixel transforms (exposure → tonemap → grade → etc.) → write to intermediate.
2. Pass 2: Read intermediate, apply sharpen + vignette + grain + dithering → write to output.

But in practice, sharpening the HDR source looks nearly identical to sharpening post-tonemap, especially with a mild sharpen strength. The single-pass approach is visually acceptable.

---

## Practical Implementation

### Descriptor Bindings

With your bindless model, you only need texture IDs passed via push constants:

| Binding/Index | Resource | Usage |
|---|---|---|
| Push constant | `src_texture_id` | HDR color buffer (sampled) |
| Push constant | `depth_texture_id` | Depth buffer (sampled, for depth viz) |
| Push constant | `output_image_id` | Output storage image |
| Push constant | `lut_texture_id` | 3D LUT for color grading |
| Push constant | `vol_texture_id` | Volumetric lighting texture |
| Push constant offset | Uniform data | Exposure, WB, contrast, etc. |

### Workgroup Size

For a full-screen post-process compute shader:

```hlsl
[numthreads(16, 16, 1)]  // 256 threads per workgroup
```

Dispatch:
```c
uint32_t groups_x = (width  + 15) / 16;
uint32_t groups_y = (height + 15) / 16;
vkCmdDispatch(cmd, groups_x, groups_y, 1);
```

Boundary check inside the shader:
```hlsl
if (pixel_coord.x >= resolution.x || pixel_coord.y >= resolution.y) return;
```

---

## Push Constant Layout

```hlsl
struct PostProcessPushConstants {
    uint  src_texture_id;
    uint  output_image_id;
    uint  depth_texture_id;
    uint  lut_texture_id;
    uint  vol_texture_id;

    uint  flags;              // bitfield: enable/disable each effect
    float exposure;           // pow(2, EV)
    float wb_temperature;
    float wb_tint;
    float contrast;
    float saturation;
    float3 lift;
    float3 gamma;
    float3 gain;
    float vignette_intensity;
    float vignette_smoothness;
    float grain_intensity;
    float sharpen_strength;
    float ca_strength;
    float lens_k1;
    float lens_k2;
    float sepia_intensity;
    float dither_strength;
    uint  tonemap_mode;       // 0=Reinhard, 1=ACES, 2=Filmic
    float gamma_value;        // typically 2.2
    uint  frame_index;        // for grain temporal variation
    float2 resolution;

    // Suballocation offset into the larger buffer (per your bindless model)
    uint  buffer_offset;
};
```

> **Note:** If this exceeds 128 bytes (the guaranteed push constant minimum), move the less-frequently-changed parameters into a uniform buffer suballocated from your large GPU buffer, and pass only the buffer offset via push constants.

---

## Full Shader Skeleton

```hlsl
// postprocess.slang

struct PostProcessParams { /* as above */ };
[[vk::push_constant]] PostProcessParams params;

// Bindless texture arrays (your existing bindless setup)
Texture2D    textures[]    : register(t0, space0);
RWTexture2D<float4> storage_images[] : register(u0, space1);
Texture3D    textures_3d[] : register(t0, space2);
SamplerState linear_sampler : register(s0);

static const uint FLAG_EXPOSURE      = (1 << 0);
static const uint FLAG_WHITE_BALANCE = (1 << 1);
static const uint FLAG_TONEMAP       = (1 << 2);
static const uint FLAG_COLOR_GRADE   = (1 << 3);
static const uint FLAG_CONTRAST      = (1 << 4);
static const uint FLAG_SATURATION    = (1 << 5);
static const uint FLAG_LGG           = (1 << 6);
static const uint FLAG_SEPIA         = (1 << 7);
static const uint FLAG_VIGNETTE      = (1 << 8);
static const uint FLAG_GRAIN         = (1 << 9);
static const uint FLAG_SHARPEN       = (1 << 10);
static const uint FLAG_CA            = (1 << 11);
static const uint FLAG_LENS_DISTORT  = (1 << 12);
static const uint FLAG_DITHER        = (1 << 13);
static const uint FLAG_DEPTH_VIZ     = (1 << 14);
static const uint FLAG_FXAA          = (1 << 15);
static const uint FLAG_GAMMA         = (1 << 16);
static const uint FLAG_VOL_APPLY     = (1 << 17);

bool has_flag(uint flag) { return (params.flags & flag) != 0; }

[numthreads(16, 16, 1)]
void computeMain(uint3 dtid : SV_DispatchThreadID) {
    int2 pixel = int2(dtid.xy);
    if (pixel.x >= int(params.resolution.x) || pixel.y >= int(params.resolution.y))
        return;

    float2 uv = (float2(pixel) + 0.5) / params.resolution;
    float2 texel_size = 1.0 / params.resolution;

    Texture2D src = textures[params.src_texture_id];

    // ═══════════════════════════════════════════
    // STAGE 1: UV remapping & neighbor reads
    // ═══════════════════════════════════════════

    float2 sample_uv = uv;

    // Lens distortion
    if (has_flag(FLAG_LENS_DISTORT)) {
        sample_uv = apply_lens_distortion(sample_uv, params.lens_k1, params.lens_k2);
    }

    float3 color;

    // Chromatic aberration
    if (has_flag(FLAG_CA)) {
        color = apply_chromatic_aberration(src, linear_sampler,
                                           sample_uv, params.resolution, params.ca_strength);
    } else {
        color = src.SampleLevel(linear_sampler, sample_uv, 0).rgb;
    }

    // Sharpen (3×3 from source)
    if (has_flag(FLAG_SHARPEN)) {
        color = apply_sharpen_to_color(src, linear_sampler, sample_uv,
                                        texel_size, params.sharpen_strength, color);
    }

    // Simple FXAA-like smoothing (from source)
    if (has_flag(FLAG_FXAA)) {
        color = simple_fxaa(src, linear_sampler, sample_uv, texel_size, color);
    }

    // ═══════════════════════════════════════════
    // STAGE 2: HDR per-pixel transforms
    // ═══════════════════════════════════════════

    // Exposure
    if (has_flag(FLAG_EXPOSURE)) {
        color *= params.exposure;
    }

    // White balance
    if (has_flag(FLAG_WHITE_BALANCE)) {
        color = white_balance(color, params.wb_temperature, params.wb_tint);
    }

    // Tone mapping
    if (has_flag(FLAG_TONEMAP)) {
        switch (params.tonemap_mode) {
            case 0: color = tonemap_reinhard(color); break;
            case 1: color = tonemap_aces(color);     break;
            case 2: color = tonemap_filmic(color);   break;
        }
    }

    // ═══════════════════════════════════════════
    // STAGE 3: LDR per-pixel grading
    // ═══════════════════════════════════════════

    // Color grading (LUT)
    if (has_flag(FLAG_COLOR_GRADE)) {
        Texture3D lut = textures_3d[params.lut_texture_id];
        color = color_grade_lut(color, lut, linear_sampler);
    }

    // Contrast
    if (has_flag(FLAG_CONTRAST)) {
        color = apply_contrast(color, params.contrast);
    }

    // Saturation
    if (has_flag(FLAG_SATURATION)) {
        color = apply_saturation(color, params.saturation);
    }

    // Lift-Gamma-Gain
    if (has_flag(FLAG_LGG)) {
        color = lift_gamma_gain(color, params.lift, params.gamma, params.gain);
    }

    // Sepia / stylized
    if (has_flag(FLAG_SEPIA)) {
        color = apply_sepia(color, params.sepia_intensity);
    }

    // Apply pre-computed volumetric lighting
    if (has_flag(FLAG_VOL_APPLY)) {
        float4 vol = textures[params.vol_texture_id].SampleLevel(
                         linear_sampler, uv, 0);
        color = color * vol.a + vol.rgb;
    }

    // Depth visualization (debug override)
    if (has_flag(FLAG_DEPTH_VIZ)) {
        Texture2D depth_tex = textures[params.depth_texture_id];
        color = visualize_depth(depth_tex, pixel);
    }

    // ═══════════════════════════════════════════
    // STAGE 4: Spatial / noise / output
    // ═══════════════════════════════════════════

    // Vignette
    if (has_flag(FLAG_VIGNETTE)) {
        color = apply_vignette(color, uv, params.vignette_intensity,
                               params.vignette_smoothness);
    }

    // Film grain
    if (has_flag(FLAG_GRAIN)) {
        color = apply_film_grain(color, uv, float(params.frame_index), params.grain_intensity);
    }

    // Gamma correction
    if (has_flag(FLAG_GAMMA)) {
        color = gamma_correct(color, params.gamma_value);
    }

    // Dithering (absolute last)
    if (has_flag(FLAG_DITHER)) {
        color = apply_dithering(color, float2(pixel));
    }

    // Write output
    storage_images[params.output_image_id][pixel] = float4(color, 1.0);
}
```

---

## What Needs a Separate Pass and Why

| Effect | Separate Pass? | Reason |
|---|---|---|
| **Volumetric lighting computation** | **YES** | Raymarching through depth + shadow maps, typically at reduced resolution, requires its own dispatch (or multiple). The *result application* is trivially included in the single pass. |
| **Full FXAA 3.11** | **YES** | Requires reading final post-processed luminance of neighbors. If you apply FXAA before tone mapping, edges may not match perceptual edges. Real FXAA needs a post-everything pass. |
| **Large-radius bloom** | (Not listed, but for context) | Requires hierarchical downscale + blur + upscale — multiple dispatches. |
| **Auto-exposure histogram** | (Not listed, but related) | Computing average luminance requires parallel reduction — a separate pass. The *result* (a single float) is consumed by the single post-process pass. |

Everything else fits in the single dispatch.

---

## Summary Table

| # | Effect | Single Pass? | Read Neighbors? | Stage |
|---|---|---|---|---|
| 1 | Tone mapping | ✅ Yes | No | 2 (HDR) |
| 2 | Gamma correction | ✅ Yes | No | 4 (Output) |
| 3 | Color grading (LUT/matrix) | ✅ Yes | No | 3 (LDR) |
| 4 | Exposure | ✅ Yes | No | 2 (HDR) |
| 5 | White balance | ✅ Yes | No | 2 (HDR) |
| 6 | Contrast/sat/LGG | ✅ Yes | No | 3 (LDR) |
| 7 | Vignette | ✅ Yes | No | 4 (Output) |
| 8 | Film grain | ✅ Yes | No | 4 (Output) |
| 9 | Sharpen (3×3) | ✅ Yes | Yes (source) | 1 (UV/Neighbor) |
| 10 | Chromatic aberration | ✅ Yes | Yes (source) | 1 (UV/Neighbor) |
| 11 | Dithering | ✅ Yes | No | 4 (Output, last) |
| 12 | Volumetric lighting **compute** | ❌ No | N/A (raymarching) | Separate pass |
| 12b | Volumetric lighting **apply** | ✅ Yes | No | 3 (LDR) |
| 13 | Lens distortion | ✅ Yes | Yes (source) | 1 (UV/Neighbor) |
| 14 | Sepia / stylized grading | ✅ Yes | No | 3 (LDR) |
| 15 | Depth visualization | ✅ Yes | No (reads depth) | 3 (LDR, debug) |
| 16 | FXAA-like (simplified) | ⚠️ Approx | Yes (source) | 1 (UV/Neighbor) |
| 16b | FXAA (full quality) | ❌ No | Yes (post-processed) | Separate pass |

### Final count:
- **14 effects** fully compatible with a single compute pass.
- **1 effect** (volumetric lighting) requires a separate pass for computation, but its application is trivially included.
- **1 effect** (FXAA) can be approximated in the single pass or done properly in a second pass.

**Bottom line: Yes, a single `vkCmdDispatch` can handle all of these except volumetric lighting computation and full-quality FXAA.**
