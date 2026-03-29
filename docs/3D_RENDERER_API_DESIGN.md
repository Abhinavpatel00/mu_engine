# 3D Renderer API Design

Status: Draft v1 (March 2026)

## Overview

This document proposes a practical 3D renderer API for the current engine architecture.

Design constraints from the existing renderer:
- Single descriptor set layout for all pipelines (bindless model)
- Single pipeline layout shared across pipelines
- GPU buffers are suballocated from large pools (offset allocator)
- Draw paths pass buffer slice offsets via push constants
- No traditional per-mesh vertex/index buffer binding model in the public API

The API below keeps those constraints explicit and first-class.

## Goals

- Keep the user-facing API small and predictable
- Make frame recording explicit and low-overhead
- Preserve bindless and offset-based data access end-to-end
- Support static and dynamic geometry with the same draw path
- Avoid hidden allocations during render submission
- Keep the ABI C-friendly and deterministic for tools/runtime integration

## Non-Goals

- Scene graph ownership or ECS policy
- Material authoring tooling
- Runtime shader compilation interface design

## API Design Rules

- Public API is C-first (plain structs, explicit init fields, no ownership ambiguity)
- Handles are POD values; `0` is always invalid
- Frame APIs are explicit and order-dependent (`begin_frame -> passes -> end_frame`)
- Render submission does not allocate GPU memory
- Draw path is offset/push-constant based (no per-draw vertex/index buffer bind API)

## Core Concepts

### 1) Handles, not raw Vulkan objects

All public objects are opaque handles. Internal data lives in renderer-owned pools.

```c
typedef uint32_t R3D_BufferHandle;
typedef uint32_t R3D_TextureHandle;
typedef uint32_t R3D_SamplerHandle;
typedef uint32_t R3D_MeshHandle;
typedef uint32_t R3D_MaterialHandle;
typedef uint32_t R3D_PipelineHandle;
typedef uint32_t R3D_PassHandle;
```

### 2) Meshes are buffer slices

A mesh references offsets into large GPU buffers. Offsets are passed to shaders via push constants.

```c
typedef struct R3D_MeshSlice {
    uint64_t vertex_stream_addr;   // base device address of pool buffer
    uint32_t vertex_offset_bytes;  // suballocation offset
    uint32_t vertex_count;

    uint64_t index_stream_addr;    // optional; can be 0 for non-indexed
    uint32_t index_offset_bytes;
    uint32_t index_count;

    uint32_t vertex_stride;
    uint32_t position_format;      // renderer enum
    uint32_t normal_format;        // renderer enum
    uint32_t uv0_format;           // renderer enum
} R3D_MeshSlice;
```

Notes:
- `*_stream_addr` is the base device address of a large pooled buffer
- `*_offset_bytes` selects the suballocation/slice for this mesh
- Backend may still use indexed draws internally, but API does not expose VB/IB object binding

### 3) Materials are bindless indices + compact constants

```c
typedef struct R3D_MaterialDesc {
    uint32_t albedo_tex_id;
    uint32_t normal_tex_id;
    uint32_t orm_tex_id;
    uint32_t emissive_tex_id;
    uint32_t sampler_id;

    float base_color_factor[4];
    float emissive_factor[3];
    float metallic_factor;
    float roughness_factor;
    float alpha_cutoff;
    uint32_t flags;
} R3D_MaterialDesc;
```

### 4) Draw item is pre-resolved CPU data

Per-draw command data is prepared before command recording.

```c
typedef struct R3D_DrawItem {
    R3D_MeshHandle mesh;
    R3D_MaterialHandle material;
    float model[16];
    uint32_t object_id;
    uint32_t sort_key;
} R3D_DrawItem;
```

## Public API Draft

## Initialization

```c
typedef struct R3D_RendererDesc {
    uint32_t width;
    uint32_t height;
    uint32_t frames_in_flight;

    uint64_t gpu_pool_size;
    uint64_t cpu_pool_size;
    uint64_t staging_pool_size;

    uint32_t max_meshes;
    uint32_t max_materials;
    uint32_t max_draw_items;
} R3D_RendererDesc;

typedef struct R3D_Renderer R3D_Renderer;

bool r3d_create(R3D_Renderer* r, const R3D_RendererDesc* desc);
void r3d_destroy(R3D_Renderer* r);
```

### API Return Conventions

- Functions that can fail return `bool`
- Handle creators return invalid handle `0` on failure
- Query functions return immutable pointers valid until next frame (or explicit reset)

### Resource Upload / Creation

```c
typedef struct R3D_UploadSlice {
    uint64_t addr;
    uint32_t offset_bytes;
    uint32_t size_bytes;
    void* mapped;
} R3D_UploadSlice;

R3D_UploadSlice r3d_alloc_gpu_slice(R3D_Renderer* r, uint32_t size, uint32_t alignment);
void r3d_free_gpu_slice(R3D_Renderer* r, uint64_t addr, uint32_t offset, uint32_t size);

void r3d_upload_to_slice(
    R3D_Renderer* r,
    uint64_t dst_addr,
    uint32_t dst_offset,
    const void* src,
    uint32_t size,
    uint32_t alignment);

R3D_MeshHandle r3d_create_mesh(R3D_Renderer* r, const R3D_MeshSlice* slice);
void r3d_destroy_mesh(R3D_Renderer* r, R3D_MeshHandle mesh);

R3D_MaterialHandle r3d_create_material(R3D_Renderer* r, const R3D_MaterialDesc* desc);
void r3d_update_material(R3D_Renderer* r, R3D_MaterialHandle mat, const R3D_MaterialDesc* desc);
void r3d_destroy_material(R3D_Renderer* r, R3D_MaterialHandle mat);
```

### Frame and Pass Recording

```c
typedef struct R3D_FrameDesc {
    float view[16];
    float proj[16];
    float view_proj[16];
    float camera_world_pos[3];
    float time_seconds;
    uint32_t frame_index;
} R3D_FrameDesc;

void r3d_begin_frame(R3D_Renderer* r, const R3D_FrameDesc* frame);
void r3d_end_frame(R3D_Renderer* r);

typedef struct R3D_PassDesc {
    const char* name;
    R3D_PipelineHandle pipeline;
    uint32_t viewport_w;
    uint32_t viewport_h;
    uint32_t clear_flags;
} R3D_PassDesc;

R3D_PassHandle r3d_begin_pass(R3D_Renderer* r, const R3D_PassDesc* pass);
void r3d_end_pass(R3D_Renderer* r, R3D_PassHandle pass);

void r3d_submit_draws(
    R3D_Renderer* r,
    R3D_PassHandle pass,
    const R3D_DrawItem* items,
    uint32_t item_count);
```

Submission contract:
- `items` memory is read during the call and not retained
- Renderer may internally sort by pipeline/material if pass flags permit
- If pass requires strict order (e.g. transparency), caller sets pass flag to disable internal reordering

### Optional Helpers

```c
void r3d_sort_draws_front_to_back(R3D_DrawItem* items, uint32_t count);
void r3d_sort_draws_back_to_front(R3D_DrawItem* items, uint32_t count);
```

## Push Constant Contract (Shared Pipeline Layout)

All graphics pipelines consume the same push constant ABI to maintain a single pipeline layout.

```c
typedef struct R3D_PushConstants {
    uint64_t vertex_stream_addr;
    uint64_t index_stream_addr;

    uint32_t vertex_offset_bytes;
    uint32_t index_offset_bytes;

    uint32_t material_id;
    uint32_t object_id;

    float model[16];
} R3D_PushConstants;
```

Rules:
- Keep this struct stable across pipelines
- Add feature-specific data only in reserved fields or a versioned extension block
- Do not create per-pipeline push layouts
- Keep size within target push constant limits across supported devices

Suggested extension reserve:

```c
typedef struct R3D_PushConstantsV1 {
    R3D_PushConstants base;
    uint32_t user0;
    uint32_t user1;
    uint32_t reserved0;
    uint32_t reserved1;
} R3D_PushConstantsV1;
```

## Typical Frame Flow

```c
r3d_begin_frame(&renderer, &frame_desc);

R3D_PassHandle gbuffer = r3d_begin_pass(&renderer, &gbuffer_desc);
r3d_submit_draws(&renderer, gbuffer, opaque_items, opaque_count);
r3d_end_pass(&renderer, gbuffer);

R3D_PassHandle lighting = r3d_begin_pass(&renderer, &lighting_desc);
r3d_submit_draws(&renderer, lighting, light_proxy_items, light_proxy_count);
r3d_end_pass(&renderer, lighting);

r3d_end_frame(&renderer);
```

## Synchronization and Lifetime Contract

- `r3d_begin_frame()` acquires frame-local transient allocators
- Any per-frame slices allocated through transient APIs are invalid after `r3d_end_frame()` of that frame-in-flight slot
- Persistent mesh/material resources must not be destroyed while referenced by in-flight frames
- API should provide deferred destroy queues keyed by frame index

Suggested helper APIs:

```c
void r3d_destroy_mesh_deferred(R3D_Renderer* r, R3D_MeshHandle mesh, uint32_t safe_after_frame);
void r3d_destroy_material_deferred(R3D_Renderer* r, R3D_MaterialHandle mat, uint32_t safe_after_frame);
```

## Pipeline and Pass Compatibility

All graphics pipelines must be created against the same descriptor set layout and pipeline layout.

Compatibility requirements:
- Same set index usage for bindless resources across all shaders
- Same push constant range/stage visibility mask
- Same convention for material ID lookup in shaders

Optional validation API:

```c
bool r3d_validate_pipeline_abi(R3D_Renderer* r, R3D_PipelineHandle pipeline);
```

## Header Skeleton (Proposed)

```c
// r3d_api.h
#ifndef R3D_API_H
#define R3D_API_H

#include <stdint.h>
#include <stdbool.h>

typedef uint32_t R3D_MeshHandle;
typedef uint32_t R3D_MaterialHandle;
typedef uint32_t R3D_PipelineHandle;
typedef uint32_t R3D_PassHandle;

typedef struct R3D_Renderer R3D_Renderer;
typedef struct R3D_RendererDesc R3D_RendererDesc;
typedef struct R3D_FrameDesc R3D_FrameDesc;
typedef struct R3D_PassDesc R3D_PassDesc;
typedef struct R3D_MeshSlice R3D_MeshSlice;
typedef struct R3D_MaterialDesc R3D_MaterialDesc;
typedef struct R3D_DrawItem R3D_DrawItem;

bool r3d_create(R3D_Renderer* r, const R3D_RendererDesc* desc);
void r3d_destroy(R3D_Renderer* r);

R3D_MeshHandle r3d_create_mesh(R3D_Renderer* r, const R3D_MeshSlice* slice);
void r3d_destroy_mesh(R3D_Renderer* r, R3D_MeshHandle mesh);

R3D_MaterialHandle r3d_create_material(R3D_Renderer* r, const R3D_MaterialDesc* desc);
void r3d_update_material(R3D_Renderer* r, R3D_MaterialHandle mat, const R3D_MaterialDesc* desc);
void r3d_destroy_material(R3D_Renderer* r, R3D_MaterialHandle mat);

void r3d_begin_frame(R3D_Renderer* r, const R3D_FrameDesc* frame);
void r3d_end_frame(R3D_Renderer* r);

R3D_PassHandle r3d_begin_pass(R3D_Renderer* r, const R3D_PassDesc* pass);
void r3d_end_pass(R3D_Renderer* r, R3D_PassHandle pass);

void r3d_submit_draws(R3D_Renderer* r, R3D_PassHandle pass, const R3D_DrawItem* items, uint32_t item_count);

const char* r3d_last_error(const R3D_Renderer* r);

#endif
```

## Data Ownership and Lifetime

- `r3d_create_*` returns stable handles until explicit destroy
- Mesh/material handles are immutable IDs; backing data can be updated through dedicated update calls
- GPU slice lifetimes are managed by caller or by higher-level asset system
- Frame-temporary allocations must come from per-frame linear/ring pools and be reset automatically

## Error Handling

- Public functions return `bool` or invalid handle (`0`) on failure
- `r3d_last_error()` returns thread-local error text for diagnostics
- Validation-only assertions remain enabled in debug builds

Suggested API:

```c
const char* r3d_last_error(const R3D_Renderer* r);
```

## Implementation Notes (Mapped to Current Engine)

- Internally map `R3D_BufferHandle` to existing buffer pools and allocator entries
- Back `r3d_submit_draws()` with existing command recording path in `passes.c`/`renderer.c`
- Material IDs should match bindless table slots where practical to avoid indirection
- Keep upload helpers aligned with current staging + copy barrier flow
- Keep descriptor set and pipeline layout singleton-style across all graphics pipelines
- Preserve current push-constant offset passing strategy; do not introduce public VB/IB bind calls

## Minimal Milestone Plan

1. Introduce public types and stub API in a new header (`r3d_api.h`)
2. Implement init/shutdown + frame begin/end wrappers
3. Implement mesh/material create/update/destroy using existing pools
4. Implement draw submission path with shared push constant ABI
5. Convert one existing 3D path to call the new API as proof of fit

## Open Questions

- Do we want a separate `R3D_RenderGraph` facade, or keep explicit pass begin/end only?
- Should mesh slices support multiple vertex streams at API level now, or after first integration?
- Should draw submission allow optional per-draw user payload (`uint32_t user0/user1`)?

## Summary

This API keeps your current renderer strengths intact: bindless descriptors, shared pipeline layout, and offset-based GPU memory usage. It also defines a narrow public surface that can evolve without exposing Vulkan internals or forcing a vertex/index binding model in the user-facing design.
