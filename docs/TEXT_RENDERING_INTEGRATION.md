# Text Rendering Integration Guide

This guide explains how to add engine-native text rendering to this renderer.

It is written for the current architecture constraints:
- Single descriptor set layout + single pipeline layout (bindless model)
- Buffer suballocation from large pools (`buffer_pool_alloc` + offset allocator)
- No vertex/index buffers for draws; geometry is generated in shader from `SV_VertexID`/`SV_InstanceID`
- Offsets/addresses passed through 256-byte push constants (`PUSH_CONSTANT` macro)

This intentionally ignores the GPU timer text path.

---

## 1) Target Feature Set (v1)

Implement a practical first version:
- Screen-space text (HUD/debug labels)
- UTF-8 input with ASCII fallback (full shaping optional in v1)
- Single font atlas texture (grayscale or SDF)
- Per-glyph color, scale, and pixel position
- Alpha blending in a dedicated text graphics pipeline

Out of scope for v1:
- Complex script shaping (Arabic/Indic), BiDi layout, fallback chains
- MSDF multi-channel edge reconstruction
- Signed-distance effects (outline/shadow) beyond basic smooth edge

---

## 2) Recommended Render Path Placement

Use one dedicated pass for text overlay:
- Render text **after `pass_ldr_to_swapchain()`**
- Render text **before `pass_imgui()`**
- Target: swapchain image as color attachment (`VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL`)

Why this order:
- Text stays crisp (not affected by tone-map or SMAA)
- Text appears beneath ImGui if desired, or above if you swap order
- Integration is minimal because `pass_imgui()` already sets up swapchain rendering

Optional world-space text later:
- Add a second text pass in HDR path (before post-process), reusing same glyph buffers

---

## 3) Data Model

Use CPU-side layout + one GPU glyph stream buffer slice per frame.

```c
typedef struct GlyphInstance
{
    float x;
    float y;
    float w;
    float h;

    float u0;
    float v0;
    float u1;
    float v1;

    uint32_t color_rgba8;
    uint32_t pad0;
    uint32_t pad1;
    uint32_t pad2;
} GlyphInstance;
```

Notes:
- Keep struct aligned to 16 bytes for clean shader reads.
- Store final per-glyph screen rectangles on CPU (x/y/w/h).
- For v1, do layout on CPU and upload packed `GlyphInstance[]` each frame.

Font atlas metadata:
- Keep glyph metrics (`advance`, `bearing`, `uv rect`) in CPU memory.
- Atlas pixels are uploaded once via `create_texture` + `renderer_upload_texture_2d` (or `load_texture` if pre-baked).
- Cache returned `TextureID` from bindless texture pool.

---

## 4) Shader Contract (No Vertex Buffers)

Follow the same pattern as existing bindless shaders: fetch from GPU address/push constants.

### Push Constant

Use the engine macro so size is exactly 256 bytes:

```c
PUSH_CONSTANT(TextPush,
    VkDeviceAddress glyph_ptr;   // renderer.gpu_base_addr + glyph_slice.offset
    uint32_t glyph_count;
    uint32_t atlas_tex_id;
    uint32_t sampler_id;

    float viewport_w;
    float viewport_h;
    float sdf_px_range; // 0 for non-SDF atlas
    uint32_t flags;
);
```

### Vertex Shader Model

- Draw call: `vkCmdDraw(cmd, 6, glyph_count, 0, 0)`
- `instanceID` picks one `GlyphInstance`
- `vertexID` in `[0..5]` builds two triangles (quad)
- Convert pixel-space position to NDC in shader using `viewport_w/h`

### Fragment Shader Model

- Sample atlas via bindless arrays:
  - `Texture2D textures[]` at binding 0
  - `SamplerState samplers[]` at binding 1
- Decode color from packed RGBA8
- Non-SDF atlas: alpha from sampled channel
- SDF atlas: smooth alpha via `smoothstep` around distance threshold

---

## 5) New Files / Integration Points

## Shader files
Add:
- `shaders/text.slang` (contains `vs_main` and `fs_main`)

`compileslang.sh` already auto-compiles any `.slang` with `vs_main` / `fs_main`, so no script changes are required.

## C-side module
Add:
- `text.h`
- `text.c`

Suggested responsibilities:
- Font atlas load/build
- UTF-8 decode + basic line layout
- Frame-local glyph stream build (`GlyphInstance[]`)
- Upload into GPU pool slice each frame
- Submit draw for text pass

## Pipeline registration
Extend `EnginePipelines` and `gfx_pipelines()`:
- Add `uint32_t text;`
- Create graphics pipeline from:
  - `compiledshaders/text.vert.spv`
  - `compiledshaders/text.frag.spv`
- Configure blend as alpha blend (`blend_alpha()`)
- Depth test/write disabled
- Topology `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`

Use existing shared pipeline layout (`renderer.bindless_system.pipeline_layout`) only.

---

## 6) Command Recording Flow

Per frame:
1. Build text queue on CPU (`text_begin_frame`, `text_draw`, `text_end_frame` style API)
2. Allocate glyph buffer slice from `renderer.gpu_pool` or frame-reset CPU-visible pool + staged copy
3. Upload glyph instances
4. Record text pass draw after `pass_ldr_to_swapchain()`

Pseudo-recording:

```c
void pass_text_overlay(void)
{
    VkCommandBuffer cmd = renderer.frames[renderer.current_frame].cmdbuf;
    uint32_t img = renderer.swapchain.current_image;

    image_transition_swapchain(cmd, &renderer.swapchain,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    flush_barriers(cmd);

    VkRenderingAttachmentInfo color = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = renderer.swapchain.image_views[img],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };

    VkRenderingInfo ri = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea.extent = renderer.swapchain.extent,
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
    };

    vkCmdBeginRendering(cmd, &ri);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        g_render_pipelines.pipelines[pipelines.text]);

    vk_cmd_set_viewport_scissor(cmd, renderer.swapchain.extent);

    TextPush push = {0};
    push.glyph_ptr   = renderer.gpu_base_addr + glyph_slice.offset;
    push.glyph_count = glyph_count;
    push.atlas_tex_id = font.atlas_texture_id;
    push.sampler_id   = renderer.default_samplers.samplers[SAMPLER_LINEAR_CLAMP];
    push.viewport_w   = (float)renderer.swapchain.extent.width;
    push.viewport_h   = (float)renderer.swapchain.extent.height;

    vkCmdPushConstants(cmd, renderer.bindless_system.pipeline_layout,
        VK_SHADER_STAGE_ALL, 0, sizeof(TextPush), &push);

    vkCmdDraw(cmd, 6, glyph_count, 0, 0);
    vkCmdEndRendering(cmd);
}
```

---

## 7) Text API Proposal (Engine-Side)

Keep API tiny for v1:

```c
void text_system_init(Renderer* r);
void text_system_shutdown(Renderer* r);

void text_begin_frame(float viewport_w, float viewport_h);
void text_draw_utf8(const char* text, float x, float y, float px_size, uint32_t color_rgba8);
void text_end_frame(Renderer* r, VkCommandBuffer cmd);

void pass_text_overlay(void);
```

Behavior:
- `text_begin_frame`: reset CPU queue
- `text_draw_utf8`: append glyph instances
- `text_end_frame`: allocate/upload glyph stream, cache slice + count
- `pass_text_overlay`: issue draw using cached per-frame slice

---

## 8) Buffer Allocation Strategy

Recommended for v1:
- Store CPU text queue in normal host memory (dynamic array)
- Upload packed glyphs to `renderer.gpu_pool` with `renderer_upload_buffer(...)`
- Keep returned `BufferSlice` valid while text data is needed

Alternative (for very dynamic text):
- Use `staging_pool` + copy each frame if you need strict frame-lifetime uploads

Important:
- Don’t keep stale pointers to staging-mapped memory across frames.
- Pass only offsets/addresses in push constants; no dedicated per-draw vertex buffer bindings.

---

## 9) Font Atlas Pipeline

Two practical options:

1. **Prebaked atlas (recommended first)**
   - Bake atlas offline (BMFont/msdf-atlas-gen/etc.)
   - Load texture + metrics at startup
   - Simplest runtime path

2. **Runtime bake with stb_truetype**
   - Build bitmap atlas on startup
   - Upload once to bindless texture pool

For this engine, option 1 is fastest to integrate and easiest to debug.

---

## 10) Blending, Gamma, and Quality Notes

- Use alpha blending (`blend_alpha()`) for text pipeline.
- If atlas is linear grayscale, tune edge thresholds in fragment shader.
- If using SDF:
  - Keep enough atlas resolution per glyph
  - Use `fwidth`-based smoothing for scale stability
- For pixel-perfect UI text, snap glyph positions to integer pixel coordinates on CPU.

---

## 11) Validation Checklist

- Pipeline compiles and hot-reloads from `shaders/text.slang`
- Text pass runs in correct order (`after blit`, `before imgui`)
- No vertex/index buffers are bound for text draw
- Push constant block remains 256 bytes (`_Static_assert` passes)
- Atlas texture sampled by bindless `TextureID`
- Resize-safe: `viewport_w/h` updated every frame
- No flicker from stale per-frame allocations

---

## 12) Minimal Rollout Plan

1. Add pre-baked font atlas + metrics loader
2. Add `text.slang` and text pipeline creation
3. Add CPU glyph queue + per-frame upload
4. Add `pass_text_overlay()` and call it before `pass_imgui()`
5. Draw one debug string (`"Hello text"`) and verify scaling/alpha
6. Expand API for alignment/wrapping only after baseline is stable

---

## 13) Common Failure Modes

- Wrong pipeline layout: text shader must use the same bindless layout as all other pipelines.
- Missing barriers/layout: swapchain image must be in `COLOR_ATTACHMENT_OPTIMAL` before rendering text.
- Incorrect address math: `glyph_ptr` must be `renderer.gpu_base_addr + slice.offset`.
- Upside-down text: verify NDC conversion and Vulkan Y-flip convention used in your shaders.
- Blurry text: rendering before post-process/SMAA or using non-integer pixel positions for UI text.

---

If you want, the next step is implementing this as `text.c/.h` + `shaders/text.slang` with a minimal single-font ASCII path first.
