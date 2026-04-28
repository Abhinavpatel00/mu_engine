# Renderer Data Flow and Refactor Plan

Status: Draft
Date: 2026-04-28
Applies to: current Vulkan renderer, model loading, animation, skinning, and draw submission

## 1. Goal

The renderer is already functional. The next step is not “more abstraction.” The next step is a faster, cleaner data model that keeps the current rendering path working while removing the hidden costs that will hurt at scale.

The weak point is not Vulkan. It is data ownership and update flow.

The current model pipeline works, but it is doing too many jobs in one file and too many passes over the same state. That creates unnecessary CPU work, makes bugs like the skinning buffer mismatch easier to introduce, and makes future changes expensive.

This document proposes a performance-first rewrite of the data flow with working-code constraints:

1. Keep the current bindless/shared-pipeline design.
2. Keep explicit buffer slices and device addresses.
3. Keep the renderer working during the transition.
4. Reduce iteration cost, allocation cost, and state coupling.

## 2. What is wrong today

`gltf_gpu_mesh.c` currently acts like four systems pretending to be one:

1. Asset loading and ownership.
2. Animation runtime.
3. Instance lifetime and per-frame state.
4. Render queue and skinning dispatch.

That means a change in one lifecycle can invalidate assumptions in the others. The result is brittle code and poor locality.

The immediate bug we already hit is a data-contract mismatch: the skinning compute shader expects a packed source vertex record, but the load path originally only provided split buffers. That is how geometry gets distorted.

## 3. The performance model

The engine should be organized around how often data changes.

### 3.1 Immutable or rarely changing data

Keep these in asset-owned memory:

1. Positions, normals, tangents, joints, weights.
2. Indices.
3. Materials.
4. Clip metadata.
5. Static submesh bounds.

This data should be uploaded once and reused by all instances.

### 3.2 Per-frame instance state

Keep these in instance-owned state:

1. Transform.
2. Color.
3. Playback state.
4. Dirty flags.
5. Per-instance skinning output references.

This data should be compact, cache-friendly, and easy to iterate.

### 3.3 Per-frame transient work

Keep these in frame-local or transient memory:

1. Upload payloads.
2. Palette update data.
3. Skinning dispatch ranges.
4. Draw queue expansion.
5. Indirect command build output.

This data should not survive beyond the frame unless it is explicitly part of a persistent asset.

## 4. Module split by lifecycle

Split the monolith by ownership and update frequency, not by feature name.

### 4.1 `model_assets.c`

Owns:

1. Mesh import.
2. GPU asset lifetime.
3. Geometry block creation.
4. Material packing.
5. Clip metadata.
6. Asset teardown.

This module answers: what exists, where it lives, and who frees it.

### 4.2 `model_instances.c`

Owns:

1. Instance create/destroy.
2. Transform and color.
3. Animation state attachment.
4. Dirty flags.
5. Skin output handles.

This module answers: which instances are live and what per-frame state they carry.

### 4.3 `animation_system.c`

Owns:

1. Clip playback.
2. Layer/blend state.
3. Palette generation.
4. Dirty propagation.

This module answers: what the current pose is and which instances need skinning.

### 4.4 `model_render.c`

Owns:

1. Queueing.
2. Sorting/grouping.
3. Indirect command build.
4. GPU submission inputs.

This module answers: what is drawn this frame and in what order.

## 5. Hot/cold split for instances

`ModelInstanceData` should not remain a single fat struct. It mixes things that update at different rates.

Current shape:

```c
typedef struct ModelInstanceData
{
	ModelHandle model;
	float transform[4][4];
	float color[4];
	AnimationState anim;
	AnimationPlaybackMode playback_mode;
	bool active;

	BufferSlice skinned_vertex_buffer;
	BufferSlice palette_buffer;
	bool palette_dirty;
} ModelInstanceData;
```

Better split:

```c
typedef struct ModelInstanceHot
{
	ModelHandle model;
	AnimationState anim;
	AnimationPlaybackMode playback_mode;
	bool palette_dirty;
	bool active;
} ModelInstanceHot;

typedef struct ModelInstanceRender
{
	float transform[4][4];
	float color[4];
} ModelInstanceRender;

typedef struct ModelInstanceSkin
{
	BufferSlice skinned_vertex_buffer;
	BufferSlice palette_buffer;
} ModelInstanceSkin;
```

Why this matters:

1. Animation update only needs hot state.
2. Draw queue only needs render state.
3. Skinning only needs skin state.
4. Cache lines stop getting dragged through unrelated work.

## 6. Sparse-set iteration

Active-instance iteration should be dense and O(1) to maintain.

The current `active_instance_ids` list is already close, but it should behave like a real sparse-set:

```c
// dense: active instance IDs packed tightly
// sparse: instance ID -> dense index

static uint32_t dense[MAX_INSTANCES];
static uint32_t sparse[MAX_INSTANCES];
static uint32_t dense_count;

static void add_instance(uint32_t id)
{
	sparse[id] = dense_count;
	dense[dense_count++] = id;
}

static void remove_instance(uint32_t id)
{
	uint32_t idx = sparse[id];
	uint32_t last = dense[--dense_count];
	dense[idx] = last;
	sparse[last] = idx;
}
```

This is better than repeated linear scans because iteration stays dense and deletion stays cheap.

If Flecs is adopted, this same pattern becomes component-query iteration instead of hand-maintained arrays. Flecs can shorten CPU-side model/instance/animation code, but it should not replace the renderer’s explicit GPU data flow.

## 7. Upload arena

The current upload path copies request payloads with heap allocations. That works, but it is too expensive and too fragmented for a high-frequency path.

Target structure:

```c
typedef struct UploadArena
{
	uint8_t* data;
	uint32_t size;
	uint32_t offset;
} UploadArena;

typedef struct UploadRequest
{
	BufferSlice dst;
	uint32_t src_offset;
	uint32_t size;
	uint32_t stage_flags;
} UploadRequest;
```

Flow:

1. Stage CPU payloads into one linear arena.
2. Store offsets instead of heap pointers.
3. Replay uploads during `model_api_prepare_frame`.
4. Insert barriers after writes.

This removes per-upload `malloc` churn and keeps the frame upload path predictable.

## 8. Packed geometry blocks

Per-submesh slices for position, UV, index, and skinning input are serviceable, but they create allocator churn and scatter the geometry state across too many objects.

Preferred shape:

```c
typedef struct GeometryBlock
{
	BufferSlice buffer;
	uint32_t position_offset;
	uint32_t normal_offset;
	uint32_t tangent_offset;
	uint32_t joint_offset;
	uint32_t weight_offset;
	uint32_t uv_offset;
	uint32_t index_offset;
} GeometryBlock;
```

Advantages:

1. Fewer allocations.
2. Less allocator metadata.
3. Better locality.
4. Simpler device-address logic.
5. Cleaner shader contracts.

For the current skinning path, the important contract is a packed source vertex layout that matches the compute shader exactly.

## 9. Skinning pipeline

The current skinning setup is correct in principle, but it should become more batched and more transient.

Current model:

1. Every animated instance owns skinned output.
2. Every dirty instance triggers its own compute work.
3. The palette is updated per instance.

That is acceptable for small scenes, but it scales poorly because it creates lots of tiny dispatches and barriers.

Better model:

```c
typedef struct SkinningBatch
{
	VkDeviceAddress src_vertex_ptr;
	VkDeviceAddress dst_vertex_ptr;
	VkDeviceAddress palette_ptr;
	uint32_t vertex_count;
	uint32_t instance_count;
} SkinningBatch;
```

Batch flow:

1. Build palettes for dirty animated instances.
2. Collect skinning ranges.
3. Dispatch compute for packed source vertices.
4. Write into transient or pooled output buffers.
5. Barrier once for graphics reads.

The goal is to reduce tiny dispatch overhead, reduce barriers, and reduce per-instance ownership churn.

## 10. Render queue and indirect build

The render queue should stay CPU-driven for now, but it needs to be explicit and efficient.

Current flow:

1. Queue model draws.
2. Expand to submeshes.
3. Sort/group.
4. Build instance table.
5. Build indirect commands.
6. Upload both arrays.

That is already a decent baseline. The next improvements are:

1. Keep the queue data in frame-local arenas.
2. Avoid repeated scans over unused slots.
3. Make grouping cheaper by storing packed geometry/material keys.
4. Move culling and indirect compaction toward GPU work when the CPU path becomes the limit.

Do not move the queue to GPU until the CPU version is clearly the bottleneck. The current problem is not that the render queue exists; it is that the data model is still too fragmented.

## 11. Animation state needs to grow up

The current animation state is enough for simple playback, but not enough for layered blending.

Current state:

```c
typedef struct AnimationState
{
	AnimationClipHandle clip;
	float time;
	float speed;
	float weight;
	bool playing;
	bool paused;
	bool loop;
} AnimationState;
```

If you want blending and layering without hacks, use an explicit layer state:

```c
typedef struct AnimLayer
{
	AnimationClipHandle current;
	AnimationClipHandle target;
	float time;
	float target_time;
	float blend_t;
	float blend_duration;
	float speed;
	uint32_t flags;
} AnimLayer;
```

This is more code up front, but it prevents the animation layer from turning into ad hoc state transitions hidden inside utility functions.

## 12. Working-code constraints

Any rewrite must preserve the current working renderer model:

1. Shared descriptor set layout.
2. Shared pipeline layout.
3. Bindless resource access.
4. Buffer slices and device addresses.
5. Push constants for routing and offsets.

That means the low-level Vulkan code stays explicit. The refactor should reduce how much custom logic has to know about every other subsystem, not hide Vulkan behind a large abstraction layer.

## 13. Why Flecs helps, and where it does not

Flecs can make the CPU-side state management shorter and less error-prone.

It helps most with:

1. Instance iteration.
2. Dirty flag filtering.
3. Animation update queries.
4. Multi-component system composition.

It does not replace:

1. Packed geometry upload.
2. Descriptor binding.
3. Compute dispatch.
4. Barrier management.
5. GPU buffer lifetime.

So the right use of Flecs here is narrow: keep it on the CPU orchestration side if it reduces code size and improves iteration locality. Do not use it to blur the renderer’s GPU ownership boundaries.

## 14. Refactor order

Do the work in this order:

1. Keep the current model pipeline working with the packed skinning contract.
2. Split asset ownership from instance state.
3. Add a frame upload arena.
4. Convert active-instance tracking to dense sparse-set iteration.
5. Split hot and cold instance storage.
6. Replace per-submesh scatter allocation with packed geometry blocks.
7. Batch skinning into transient arenas.
8. Keep render queueing focused on submission only.

If Flecs is adopted, introduce it only after the ownership boundaries are clear. Otherwise it will just move the same ambiguity into a different API.

## 15. Final rule

For every new feature, ask:

1. Which lifecycle owns this data?
2. Which system mutates it?
3. Which stage reads it?

If the answer is not obvious, the feature is probably in the wrong module.
