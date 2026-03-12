# Why the blocks looked dim (and the fix)

## Symptom
Voxel blocks appeared noticeably darker than their source textures.

## Root cause
The render path was mixing **linear** and **display (sRGB/nonlinear)** color spaces incorrectly:

1. `triangle.slang` samples block textures (stored as `VK_FORMAT_R8G8B8A8_SRGB`) into linear shader color.
2. That color is rendered into HDR (`R16G16B16A16_SFLOAT`) correctly in linear space.
3. `postprocess.slang` tonemapped the color, but wrote it directly to `ldr_color` (`VK_FORMAT_R8G8B8A8_UNORM`) **without sRGB encoding**.
4. The engine then `vkCmdBlitImage` copies `ldr_color` into the sRGB swapchain image.

Because blit/copy paths do not perform sRGB conversion for you, linear values ended up being treated like display-space values. That makes midtones look too dark ("dim").

## Fix applied
In `shaders/postprocess.slang`, after tonemapping, output is now explicitly encoded to sRGB:

- `color = tonemapACES(color);`
- `color = toSrgb(saturate(color));`

This matches what the display expects and restores perceived brightness.

## Why this fix is minimal and safe
- No descriptor/pipeline layout changes.
- No buffer layout changes.
- Keeps existing bindless + push-constant flow.
- Only adjusts final transfer color encoding in postprocess.

## Notes
A cleaner long-term path is to do final presentation through a fullscreen graphics pass into the sRGB swapchain (letting attachment conversion handle encoding), instead of blitting from an UNORM intermediate.
