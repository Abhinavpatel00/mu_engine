# Efficient 2D and 3D Game Engine Architecture

Status: Proposal for this repository

## Purpose

This document describes an efficient architecture for growing the current renderer into a practical 2D and 3D game engine. It is written for the code that exists in this repo, not for a generic engine.

The current engine already has strong low-level decisions:

- Vulkan renderer with one global bindless descriptor set.
- Shared pipeline layout and push-constant based draw configuration.
- Large CPU/GPU/staging buffer pools with suballocated `BufferSlice` data.
- Device-address shader fetch for model, material, instance, sprite, and text data.
- HDR render targets, postprocess, SMAA, LDR swapchain copy, GPU profiling, and ImGui support.
- A C-first style with explicit lifetime and frame order.
- A dense ECS implementation in `mu/mu_ecs.h` that matches the desired data-oriented direction.

The recommended architecture keeps those constraints first-class. The engine should not grow a second resource model for 2D, a traditional per-object Vulkan binding model for 3D, or hidden per-frame allocation behavior.

## Target Shape

```text
Game / Tools
    |
    v
World Layer
    |- Entity manager
    |- Dense component pools
    |- Scene handles
    |- Asset handles
    |
    v
Simulation Systems
    |- Input
    |- Transform
    |- Animation
    |- Physics / collision
    |- Gameplay logic
    |
    v
Render Extraction
    |- Camera extraction
    |- 2D sprite/text extraction
    |- 3D mesh/model extraction
    |- Visibility and sort keys
    |- Packed transient instance streams
    |
    v
Renderer Frontend
    |- Stable handles
    |- Render queues
    |- Upload scheduling
    |- Pass submission
    |
    v
Vulkan Backend
    |- Bindless descriptor set
    |- Buffer pools and offset allocator
    |- Pipeline cache
    |- Dynamic rendering passes
    |- Barriers and presentation
```

The important split is between simulation ownership and render submission. Gameplay components own game state. Render extraction builds temporary packed arrays. The Vulkan backend only sees dense buffers, bindless IDs, handles, offsets, and draw counts.

## Architectural Rules

1. Public engine APIs should be C-first: plain structs, explicit init/shutdown, stable handles, and no hidden ownership.
2. `0` or `UINT32_MAX` should be the invalid value for every handle type, consistently documented per subsystem.
3. Frame APIs should be explicit: begin frame, collect work, prepare uploads, record passes, submit, reset transient state.
4. No per-object Vulkan objects in hot paths. Meshes, instances, sprites, text, lights, and camera data should live in packed buffers.
5. Render submission should not allocate persistent GPU memory. Persistent resources are created through asset/resource systems before submission.
6. Per-frame data should use linear or ring allocation and be released when the frame fence completes.
7. Hot iteration should use dense arrays, sort keys, and sequential writes.
8. Renderer-facing data should be offsets, device addresses, bindless texture IDs, sampler IDs, and compact constants.

## Core Subsystems

### Platform and Frame Loop

The current loop in `main.c` is the right high-level shape:

```text
poll/rebuild
frame_start
simulation update
render extraction
prepare uploads
record render passes
postprocess
present transition
submit_frame
```

The next step is to make the middle of the frame more structured:

```c
engine_begin_frame(&engine, dt);
world_update(&engine.world, input, dt);
render_world_extract(&engine.render_world, &engine.world);
renderer_prepare_frame(&renderer, &engine.render_world, cmd);
renderer_execute_frame(&renderer, &engine.render_world, cmd);
engine_end_frame(&engine);
```

This keeps the existing Vulkan lifecycle but removes direct model-specific logic from the application loop.

### Asset System

Assets should be long-lived and handle-based. They should not also own per-frame draw queues.

Recommended asset groups:

- `TextureAsset`: image, bindless texture ID, default sampler ID, dimensions, format.
- `MeshAsset`: vertex/index slices, vertex layout, bounds, submesh range.
- `MaterialAsset`: bindless texture IDs plus compact constants.
- `ModelAsset`: list of submeshes and material bindings.
- `SpriteAtlasAsset`: texture ID plus packed regions.
- `FontAsset`: glyph atlas, glyph metrics, shaping/cache data.

The current GLTF path in `gltf_gpu_mesh.c` should evolve into two systems:

- `ModelAssetSystem`: load, upload, own residency, provide `ModelHandle`.
- `ModelRenderQueue`: accept per-frame draw requests, sort, build instance and indirect data, draw.

That separation is already described in `docs/MODEL_API_REFACTOR.md` and should be treated as the 3D resource direction.

### World and ECS Layer

Use `mu/mu_ecs.h` style storage for gameplay-owned data:

- Entity IDs are weak handles with generation validation.
- Components are dense arrays.
- Removal is swap-erase by default.
- Systems process homogeneous arrays.
- Render extraction reads components and writes packed transient render records.

Recommended component families:

```text
Shared
    Transform2D / Transform3D
    Name / DebugTag
    Visibility

2D
    Sprite2D
    Text2D
    Tilemap2D
    Camera2D
    Animator2D

3D
    MeshRenderer3D
    ModelRenderer3D
    Light3D
    Camera3D
    Animator3D
```

Avoid putting Vulkan objects, mapped pointers, or upload state in components. Components should reference engine handles.

## Unified Render Extraction

Render extraction converts world state into renderer-owned transient arrays. It is the boundary between gameplay and Vulkan.

```text
World components
    -> visible 2D sprites
    -> visible 2D text
    -> visible 3D meshes/models
    -> lights
    -> cameras
    -> debug draw
    -> packed render queues
```

Each extracted item should be small and sortable:

```c
typedef struct RenderItem2D {
    uint64_t sort_key;
    uint32_t texture_id;
    uint32_t sampler_id;
    uint32_t material_flags;
    float transform_or_rect[8];
    float uv_rect[4];
    float color[4];
} RenderItem2D;

typedef struct RenderItem3D {
    uint64_t sort_key;
    uint32_t mesh_id;
    uint32_t material_id;
    uint32_t submesh_id;
    float model[16];
    float color[4];
} RenderItem3D;
```

The exact structs can change, but the rule should remain: gameplay produces compact render intent, not Vulkan commands.

## 3D Rendering Architecture

The 3D path should keep the current shader model used by `shaders/gltf_minimal.slang`:

- Push constants contain table pointers and camera data.
- Instance data contains model ID, transform, and color.
- Model metadata contains GPU addresses for positions, UVs, indices, and material ID.
- Material metadata contains bindless texture and sampler IDs.
- Vertex shader fetches through device addresses.

Recommended 3D frame path:

```text
ModelRenderer3D components
    -> RenderItem3D array
    -> cull against active camera
    -> sort by pipeline/material/mesh/depth
    -> build instance buffer
    -> build indirect draw commands
    -> bind one pipeline and descriptor set
    -> push model/material/instance table addresses
    -> draw indirect batches
```

Sort key layout:

```text
bits 63..56 pass
bits 55..48 pipeline
bits 47..32 material
bits 31..16 mesh/submesh
bits 15..00 depth or stable tie-breaker
```

Opaque 3D should sort by pipeline/material/mesh and optionally front-to-back inside groups. Transparent 3D should be extracted into a separate queue and sorted back-to-front after opaque depth has been written.

### 3D Resource Layout

Use persistent GPU pool allocations for mesh data:

```text
GPU pool
    positions stream
    normal/tangent stream
    uv stream
    index stream
    model metadata table
    material table
```

Use transient per-frame allocations for:

- instance data
- indirect command data
- visible object lists
- per-camera constants
- light lists

Do not resize persistent model buffers mid-frame. Queue uploads, process them in the frame upload phase, and make new resources visible after the required transfer-to-shader barriers.

### 3D Phases

1. Split model assets from draw queue.
2. Support all GLTF primitives/submeshes, not only one mesh.
3. Add material-complete sort keys.
4. Add CPU frustum culling using submesh/model bounds.
5. Move indirect command construction to GPU compute when the CPU path is stable.

## 2D Rendering Architecture

The 2D path should reuse the same renderer backend and the current `sprite.slang` approach:

- A single quad topology is generated in the vertex shader.
- Instance records are read from a GPU buffer through a device address.
- Textures are sampled through bindless texture IDs.
- Sampler choice is explicit and shared.
- 2D cameras use orthographic matrices uploaded like any other camera.

Recommended 2D frame path:

```text
Sprite2D/Text2D/Tilemap2D components
    -> RenderItem2D array
    -> camera/layer filtering
    -> sort by pass/layer/material/texture/order
    -> write packed instance stream
    -> bind sprite/text pipeline and descriptor set
    -> push instance pointer, camera matrix, sampler ID
    -> draw instanced quads
```

### 2D Sort Key

```text
bits 63..56 pass          world, ui, debug
bits 55..48 layer         coarse gameplay layer
bits 47..40 material      blend/filter/shader variant
bits 39..24 texture       bindless texture or atlas page
bits 23..08 order         explicit order or y-sort bucket
bits 07..00 stable id     deterministic tie-breaker
```

For alpha-blended sprites, correctness may require sorting by layer/order instead of maximizing texture batches. Keep this as a per-pass policy:

- World opaque tiles: batch aggressively by material and texture.
- World alpha sprites: sort by layer/y/order, then batch adjacent compatible ranges.
- UI: stable order first, batching second.
- Debug: simple append order is acceptable.

### 2D Instance Record

Keep one fixed-size record for the hot path. The current shader expects 16 dwords, which is a good baseline:

```c
typedef struct SpriteInstanceGpu {
    float x, y;
    float sx, sy;
    float rotation;
    float depth;
    float opacity;
    uint32_t texture_id;
    float u0, v0, u1, v1;
    float r, g, b, a;
} SpriteInstanceGpu;
```

For tilemaps, do not emit every static tile every frame once maps get large. Use chunked tile layers:

- Static chunks are built once into GPU buffers.
- Dirty chunks are rebuilt when edited.
- Dynamic sprites use the normal per-frame instance stream.
- Camera culling operates at chunk bounds first, then optional per-tile refinement.

## Camera System

Use one camera system for both 2D and 3D, as described in `docs/CAMERA_2D_3D_ABSTRACTION_PLAN.md`.

The renderer-facing camera payload should be mode-independent:

```c
typedef struct CameraGpu {
    float view[16];
    float proj[16];
    float view_proj[16];
    float inv_view_proj[16];
    float position_ws[4];
    float viewport_size[4];
} CameraGpu;
```

2D and 3D differ in how the matrices are built, not in how render passes consume them.

Rules:

- Upload active camera data once per frame.
- Pass camera offsets or device addresses through push constants.
- Keep camera controllers outside the renderer.
- Use reverse-Z consistently in 3D depth passes.
- Keep 2D orthographic depth policy explicit so UI/world/debug layers do not fight 3D depth.

## Render Pass Graph

The current pass order is manually recorded:

```text
HDR color/depth transition
3D rendering
postprocess
SMAA
LDR to swapchain
present transition
```

That can remain manual for now, but pass ownership should be made explicit:

```text
Frame Upload Pass
Depth / Opaque 3D Pass
Sky Pass
Transparent 3D Pass
2D World Pass
Text Pass
UI Pass
Debug Pass
Postprocess Pass
SMAA Pass
Swapchain Resolve Pass
ImGui Pass
Present
```

Each pass should declare:

- color/depth inputs and outputs
- load/store behavior
- required layout
- pipeline stage/access needs
- queue type
- whether sorting may reorder draws

A full render graph is optional at this stage. A small static pass table plus barrier batching is enough until the engine has many conditional passes.

## Memory Model

Keep three memory lifetimes separate:

### Persistent GPU Data

Examples:

- mesh streams
- material tables
- texture images
- atlas images
- static tile chunks

Storage:

- GPU pool
- bindless texture/sampler tables
- offset allocator for free/reuse

### Per-Frame Transient Data

Examples:

- camera constants
- 2D sprite instances
- text vertices/indices
- 3D draw instances
- indirect commands
- light lists
- visible item lists

Storage:

- CPU-visible linear frame pool, or staging ring plus GPU destination slices.
- Reset after the frame fence completes.

### CPU Asset Data

Examples:

- decoded GLTF structures
- image decode buffers
- font glyph metadata
- editor-only scene data

Storage:

- owned by asset systems
- freed after upload when not needed for hot reload or tooling

## Threading Model

The first scalable threading target is render extraction, not Vulkan command recording.

Recommended progression:

1. Single-thread all systems until APIs are stable.
2. Parallelize independent gameplay systems with explicit read/write component access.
3. Parallelize render extraction by component ranges into per-thread scratch arrays.
4. Merge scratch arrays into final sorted render queues.
5. Add secondary command buffers only if profiling shows command recording is a bottleneck.

Per-thread extraction should never write shared counters directly in hot loops. Give each worker a local output buffer, then merge.

## Data-Oriented Performance Guidelines

- Store hot frame data in arrays, not pointer graphs.
- Avoid per-entity heap allocations.
- Avoid strings in render hot paths; resolve names to handles before the frame.
- Sort once, consume once.
- Batch uploads into contiguous copies.
- Keep shader records fixed-size and aligned.
- Separate static, dynamic, and streaming allocations.
- Prefer generation-checked handles over raw pointers between systems.
- Use dirty flags for expensive derived data, but avoid over-fragmenting update paths.
- Profile before moving CPU work to GPU compute.

## Error Handling and Debuggability

Every public subsystem should expose:

- init result
- shutdown
- validation checks in debug builds
- handle validity checks
- human-readable debug names where possible
- counters for allocated resources, queued draws, uploaded bytes, and culled items

Recommended debug views:

- render queue counts by pass
- GPU pool usage
- staging ring usage
- bindless texture allocation table
- active camera matrices
- 2D layer/order visualization
- 3D bounding boxes and frustum culling results

## Suggested File Layout

```text
engine.h / engine.c
    high-level init, frame loop coordination

assets.h / assets.c
    texture, model, material, atlas, font handles

camera_system.h / camera_system.c
    shared 2D/3D camera state and GPU upload

render_world.h / render_world.c
    extracted render queues and transient frame data

render_2d.h / render_2d.c
    sprite, tilemap, text queue preparation and draw submission

render_3d.h / render_3d.c
    model/mesh queue preparation and draw submission

scene.h / scene.c
    optional world/scene facade over ECS storage

passes.h / passes.c
    explicit pass order and render target transitions
```

Existing files can be migrated gradually. Do not move everything at once.

## Migration Plan

### Phase 1: Stabilize Resource Boundaries

- Split GLTF asset ownership from the per-frame model draw queue.
- Replace path-based draw submission in hot code with `ModelHandle`.
- Add material and submesh-aware 3D sort keys.
- Keep current shaders and pass order.

### Phase 2: Introduce Render World

- Add a transient `RenderWorld` or `RenderFrame` object.
- Move 3D queued draw data into it.
- Add 2D sprite queue data using the current sprite shader.
- Make `main.c` submit through render-world APIs instead of calling model APIs directly.

### Phase 3: Add Camera System

- Replace direct camera pass-through with shared `CameraGpu` upload.
- Support one active 3D camera and one active 2D/UI camera first.
- Pass camera device addresses or offsets into 2D and 3D shaders consistently.

### Phase 4: ECS Integration

- Use `mu_ecs` component pools for transforms, sprites, model renderers, lights, and cameras.
- Add render extraction systems that read ECS data and write `RenderWorld`.
- Keep asset systems independent from ECS.

### Phase 5: Larger-Scale Optimization

- Add CPU frustum culling.
- Add static tile chunks.
- Add GPU-built indirect commands for large 3D scenes.
- Add clustered or tiled light lists only after scene/lights justify it.
- Replace manual pass sequencing with a small static render graph if pass count grows.

## Success Criteria

The architecture is working when:

- 2D and 3D use the same renderer backend and resource lifetime model.
- Gameplay components never own Vulkan objects.
- Frame rendering can be inspected as packed queues and passes.
- Adding a sprite, model, text item, or camera does not require descriptor set changes.
- Per-frame allocations are linear/ring-based and bounded.
- Draw submission is handle-based, sorted, and batchable.
- The application loop stays small while the engine owns frame orchestration.

