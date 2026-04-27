# Renderer API Reference

This document describes the current renderer API and runtime behavior in the codebase as of April 2026.

It is based on the actual public interfaces in:
- `vk.h`
- `renderer.h`
- `passes.h`
- `gltf_gpu_mesh.h`

## 1. Renderer Model

The renderer is a Vulkan dynamic-rendering backend with these core traits:
- Single bindless descriptor set and single pipeline layout shared across all pipelines.
- Resource indexing by bindless IDs (`TextureID`, `SamplerID`) and buffer device addresses.
- Buffer suballocation through `BufferPool` (`LINEAR`, `RING`, `TLSF`).
- Reverse-Z depth defaults for 3D perspective (`VK_COMPARE_OP_GREATER`).
- Frame helpers that handle fences, swapchain acquire/present, camera update, and resize.
- Render graph style done manually through explicit pass functions and explicit image transitions.

Global renderer state is exposed as:

```c
extern Renderer renderer;
extern EnginePipelines pipelines;
```

## 2. Key Limits and Bindless Bindings

From `vk.h`:

```c
#define BINDLESS_TEXTURE_BINDING 0
#define BINDLESS_SAMPLER_BINDING 1
#define BINDLESS_STORAGE_IMAGE_BINDING 2

#define MAX_BINDLESS_TEXTURES 65536
#define MAX_BINDLESS_SAMPLERS 256
#define MAX_BINDLESS_STORAGE_BUFFERS 65536
#define MAX_BINDLESS_UNIFORM_BUFFERS 16384
#define MAX_BINDLESS_STORAGE_IMAGES 16384
#define MAX_BINDLESS_VERTEX_BUFFERS 65536
#define MAX_BINDLESS_INDEX_BUFFERS 65536
#define MAX_BINDLESS_MATERIALS 65536
#define MAX_BINDLESS_TRANSFORMS 65536
```

## 3. Core Types You Use Most

- `RendererDesc`: startup configuration for Vulkan instance/device/swapchain/features/pool sizes.
- `Renderer`: runtime state (device, queues, swapchain, frame contexts, pools, render targets, samplers, bindless system, profiling).
- `BufferPool` / `BufferSlice`: suballocated GPU/CPU/staging memory.
- `Texture` + `TextureID`: bindless texture objects.
- `RenderTarget`: managed offscreen image with per-mip state tracking.
- `GraphicsPipelineConfig`: dynamic rendering pipeline creation config.
- `Camera`: unified 2D/3D camera structure.

## 4. Initialization and Shutdown

### High-level entry points

```c
void graphics_init(void);
void gfx_pipelines(void);
```

`graphics_init()` currently:
1. Initializes Volk and GLFW platform hints.
2. Fills `RendererDesc` (bindless counts, swapchain prefs, pool sizes, validation flags).
3. Calls `renderer_create(&renderer, &desc)`.
4. Calls `gfx_pipelines()`.

### Low-level renderer lifecycle

```c
void renderer_create(Renderer* r, RendererDesc* desc);
void renderer_destroy(Renderer* r);
```

## 5. Frame Lifecycle API

Frame helpers are inline in `vk.h`:

```c
static MU_INLINE void frame_start(Renderer* renderer, Camera* cam);
static MU_INLINE void submit_frame(Renderer* r);
```

### `frame_start` responsibilities

- Advances `current_frame` modulo `MAX_FRAMES_IN_FLIGHT`.
- Tracks CPU frame/wait/active timing.
- Polls input/window events.
- Detects resize and recreates swapchain/render targets.
- Updates camera (including 3D mouse-look + movement handling).
- Waits/reset fences and command pool.
- Resets frame allocators:
  - `buffer_pool_linear_reset(&renderer->cpu_pool)`
  - `buffer_pool_ring_free_to(&renderer->staging_pool, frame->staging_tail)`
- Acquires swapchain image.

### `submit_frame` responsibilities

- Saves ring-tail for staging lifetime tracking.
- Submits command buffer with `vkQueueSubmit2`.
- Signals swapchain render-finished semaphore.
- Presents via `vk_swapchain_present`.

## 6. Buffer Pools and Uploads

### Pool API

```c
bool buffer_pool_init(Renderer* r,
                      BufferPoolType type,
                      BufferPool* pool,
                      VkDeviceSize size_bytes,
                      VkBufferUsageFlags usage,
                      VmaMemoryUsage memory_usage,
                      VmaAllocationCreateFlags alloc_flags,
                      oa_uint32 max_allocs);

void buffer_pool_destroy(Renderer* r, BufferPool* pool);
void buffer_pool_linear_reset(BufferPool* pool);
void buffer_pool_ring_free_to(BufferPool* pool, uint32_t offset);
BufferSlice buffer_pool_alloc(BufferPool* pool, VkDeviceSize size_bytes, VkDeviceSize alignment);
void buffer_pool_free(BufferSlice slice);
```

### Upload helpers

```c
bool renderer_upload_buffer_to_slice(Renderer* r,
                                     VkCommandBuffer cmd,
                                     BufferSlice dst_slice,
                                     const void* src_data,
                                     VkDeviceSize size_bytes,
                                     VkDeviceSize staging_alignment);

BufferSlice renderer_upload_buffer(Renderer* r,
                                   VkCommandBuffer cmd,
                                   const void* src_data,
                                   VkDeviceSize size_bytes,
                                   VkDeviceSize staging_alignment,
                                   VkDeviceSize dst_alignment);

bool renderer_upload_texture_2d(Renderer* r,
                                VkCommandBuffer cmd,
                                Texture* tex,
                                const void* pixels,
                                VkDeviceSize size_bytes,
                                uint32_t width,
                                uint32_t height,
                                uint32_t mip_level);
```

Staging slices are frame-lifetime transient. Do not cache staging mapped pointers across frames.

## 7. Texture and Sampler API

### Textures

```c
typedef uint32_t TextureID;

TextureID create_texture(Renderer* r, const TextureCreateDesc* desc);
void      destroy_texture(Renderer* r, TextureID id);
TextureID load_texture(Renderer* r, const char* path);
TextureID load_texture_id_in_range(Renderer* r, const char* path, uint32_t max_id);
```

`TextureCreateDesc`:

```c
typedef struct
{
    uint32_t          width;
    uint32_t          height;
    uint32_t          mip_count;
    VkFormat          format;
    VkImageUsageFlags usage;
    const char*       debug_name;
} TextureCreateDesc;
```

### Samplers

```c
typedef uint32_t SamplerID;

SamplerID create_sampler(Renderer* r, const SamplerCreateDesc* desc);
void      destroy_sampler(Renderer* r, SamplerID id);
```

Default samplers are exposed in `renderer.default_samplers.samplers[...]` with enum `DefaultSamplerID`.

## 8. Render Targets and Transitions

### Render target lifecycle

```c
bool rt_create(Renderer* r, RenderTarget* rt, const RenderTargetSpec* spec);
bool rt_resize(Renderer* r, RenderTarget* rt, uint32_t width, uint32_t height);
void rt_destroy(Renderer* r, RenderTarget* rt);
```

### Transition helpers

```c
void image_transition_swapchain(VkCommandBuffer cmd,
                                FlowSwapchain* sc,
                                VkImageLayout new_layout,
                                VkPipelineStageFlags2 new_stage,
                                VkAccessFlags2 new_access);

void image_transition_simple(VkCommandBuffer cmd,
                             VkImage image,
                             VkImageAspectFlags aspect,
                             VkImageLayout old_layout,
                             VkImageLayout new_layout);

void cmd_transition_all_mips(VkCommandBuffer cmd,
                             VkImage image,
                             ImageState* state,
                             VkImageAspectFlags aspect,
                             uint32_t mipCount,
                             VkPipelineStageFlags2 newStage,
                             VkAccessFlags2 newAccess,
                             VkImageLayout newLayout,
                             uint32_t newQueueFamilyi);

void cmd_transition_mip(VkCommandBuffer cmd,
                        VkImage image,
                        ImageState* state,
                        VkImageAspectFlags aspect,
                        uint32_t mip,
                        VkPipelineStageFlags2 newStage,
                        VkAccessFlags2 newAccess,
                        VkImageLayout newLayout,
                        uint32_t newQueueFamily);
```

Inline wrappers:
- `rt_transition_mip(...)`
- `rt_transition_all(...)`

Barrier flush helper:

```c
void flush_barriers(VkCommandBuffer cmd);
```

## 9. Pipeline API

### Pipeline creation

```c
typedef uint32_t PipelineID;

PipelineID pipeline_create_graphics(Renderer* r, GraphicsPipelineConfig* cfg);
PipelineID pipeline_create_compute(Renderer* r, const char* path);
VkPipeline pipeline_get(PipelineID id);
```

### Rebuild/hot reload controls

```c
void pipeline_mark_dirty(const char* changed_shader);
void pipeline_rebuild(Renderer* r);
```

### Cache save

```c
void pipeline_cache_save(VkDevice device,
                         VkPhysicalDevice phys,
                         VkPipelineCache cache,
                         const char* path);
```

### Engine pipeline handles

`EnginePipelines` currently tracks:
- `fullscreen`
- `postprocess` (compute)
- `gltf_minimal`
- `triangle`
- `triangle_wireframe`
- `sprite`
- `slug_text`
- `beam`
- `sky`

SMAA pipelines are tracked separately in `renderer.smaa_pipelines`.

## 10. Camera API (2D/3D)

Useful camera functions:

```c
static FORCE_INLINE void camera_defaults_3d(Camera* cam);
static FORCE_INLINE void camera_defaults_2d(Camera* cam, uint32_t viewport_width, uint32_t viewport_height);
static FORCE_INLINE void camera_set_mode(Camera* cam, CameraMode mode);
static FORCE_INLINE void camera_set_projection(Camera* cam, CameraProjection projection);
static FORCE_INLINE void camera_set_viewport(Camera* cam, uint32_t width, uint32_t height);

static FORCE_INLINE void camera2d_set_bounds(Camera* cam, float min_x, float min_y, float max_x, float max_y);
static FORCE_INLINE void camera2d_clear_bounds(Camera* cam);
static FORCE_INLINE void camera2d_set_position(Camera* cam, float x, float y);
static FORCE_INLINE void camera2d_pan(Camera* cam, float dx_world, float dy_world);
static FORCE_INLINE void camera2d_zoom(Camera* cam, float zoom_delta);

static FORCE_INLINE void camera3d_set_position(Camera* cam, float x, float y, float z);
static FORCE_INLINE void camera3d_set_rotation_yaw_pitch(Camera* cam, float yaw, float pitch);

static FORCE_INLINE void camera_update_matrices(Camera* cam, float aspect, bool reverse_z);
static FORCE_INLINE void camera_extract_frustum(Frustum* out_frustum, const mat4 view_proj);
```

`frame_start` calls `camera_update_matrices(cam, aspect, true)` in the default flow.

## 11. Model API (MeshX + GLTF)

Public model API from `gltf_gpu_mesh.h`:

```c
bool model_api_init(uint32_t max_models, uint32_t instance_capacity);
void model_api_shutdown(void);

bool model_api_load_meshx(const char* path, ModelHandle* out_model);
bool model_api_find_or_load_meshx(const char* path, ModelHandle* out_model);
bool model_api_load_gltf(const char* path, ModelHandle* out_model);
bool model_api_find_or_load_gltf(const char* path, ModelHandle* out_model);
void model_api_unload(ModelHandle model);

void model_api_begin_frame(const Camera* cam);
bool model_api_draw(ModelHandle model, const float model_matrix[4][4], const float color[4]);
bool draw3d(ModelHandle model, const float model_matrix[4][4], const float color[4]);
bool draw_model(const char* path, const float model_matrix[4][4]);

void model_api_prepare_frame(VkCommandBuffer cmd);
void model_api_draw_queued(VkCommandBuffer cmd);
void model_api_flush_frame(VkCommandBuffer cmd);
```

### Current draw path details

- Draws are queued CPU-side, sorted/grouped per submesh.
- Instance and indirect command arrays are uploaded each frame.
- Draw uses `vkCmdDrawIndirect`.
- Geometry/material/instance data is accessed in shader via push constants containing device addresses.
- No per-draw vertex/index buffer binding in this model path.

## 12. Pass API

From `passes.h`:

```c
void post_pass();
void pass_smaa();
void pass_ldr_to_swapchain();
void pass_imgui();
```

Current pass chain in `main.c`:
1. Scene rendering to HDR (`model_api_draw_queued` inside dynamic rendering).
2. `post_pass()` compute to LDR.
3. `pass_smaa()` (edge/weight/blend passes).
4. `pass_ldr_to_swapchain()` blit.
5. Swapchain transition to present and submit.

## 13. Push Constants Contract

The engine enforces a 256-byte push constant layout via macro:

```c
#define PUSH_CONSTANT(name, BODY) ... _Static_assert(sizeof(name) == 256, "Push constant != 256");
```

Requirements:
- Struct alignment uses `ALIGNAS(16)`.
- Total pushed type size is always 256 bytes.
- Keep fields 16-byte aware to avoid host/shader packing mismatch.

## 14. Canonical Frame Skeleton

```c
graphics_init();
model_api_init(256, 8192);

Camera cam = {0};
camera_defaults_3d(&cam);

while(!glfwWindowShouldClose(renderer.window))
{
    pipeline_rebuild(&renderer);
    frame_start(&renderer, &cam);

    model_api_begin_frame(&cam);
    // queue draw_model(...) / model_api_draw(...)

    VkCommandBuffer cmd = renderer.frames[renderer.current_frame].cmdbuf;
    vk_cmd_begin(cmd, false);

    // transitions + dynamic rendering
    model_api_prepare_frame(cmd);
    model_api_draw_queued(cmd);

    post_pass();
    pass_smaa();
    pass_ldr_to_swapchain();

    // transition swapchain -> present
    flush_barriers(cmd);
    vk_cmd_end(cmd);

    submit_frame(&renderer);
}

model_api_shutdown();
renderer_destroy(&renderer);
```

## 15. Notes and Constraints

- Renderer defaults to a bindless workflow; avoid per-pipeline descriptor layouts.
- Suballocated slices are first-class and expected in most paths.
- `renderer.bindless_system.set` is bound for both graphics and compute in the frame loop.
- Render target transitions are explicit; call `flush_barriers(cmd)` after enqueuing transitions.
- `model_api_load_gltf` attempts meshx sidecar first when available.
- GLB base-color extraction now supports embedded image name + MIME fallback (no URI required).

## 16. Related Headers

- `vk.h`: core renderer API, types, inline frame/camera helpers.
- `renderer.h`: global renderer + engine pipeline declarations, startup entry points.
- `passes.h`: frame pass entry points.
- `gltf_gpu_mesh.h`: model streaming, loading, and draw queue API.
