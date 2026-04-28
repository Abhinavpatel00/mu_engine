# Model Pipeline Guide

Status: Current implementation guide
Date: 2026-04-28
Applies to: `gltf_gpu_mesh.c`, model loading, animation, skinning, and queued model rendering

## 1. What the model pipeline does

The model pipeline turns imported mesh data into GPU-ready assets, tracks per-instance animation state, optionally runs compute skinning, and finally submits grouped indirect draws.

In the current engine, the pipeline is split into four runtime phases:

1. Load and import model data.
2. Create or update model instances.
3. Prepare animation and skinning for the frame.
4. Queue and draw the model with indirect commands.

The important detail is that the rendering path does not bind per-mesh vertex or index buffers in the traditional Vulkan style. Instead, it uses suballocated buffer slices, device addresses, and push constants to tell shaders where the data lives.

## 2. Main data structures

The current model path centers on these groups of state:

### 2.1 Asset data

`ModelAssetSystem` owns the long-lived imported data:

1. `ModelData` records the model-wide submesh, material, clip, and feature flags.
2. `Submesh` stores per-submesh GPU slices and static metadata.
3. `MaterialData` and `MaterialGpu` store CPU-side and GPU-packed material state.
4. `AnimationClipData` stores clip names and durations.

This data lives for the lifetime of the model and is shared by all instances.

### 2.2 Instance data

`ModelInstanceSystem` owns per-instance runtime state:

1. `ModelInstanceData.model` points at the shared model asset.
2. `transform` and `color` are the draw-facing instance properties.
3. `anim` stores playback state.
4. `skinned_vertex_buffer` and `palette_buffer` are used for compute skinning.
5. `palette_dirty` marks whether the instance needs skinning work this frame.

### 2.3 Queue data

`ModelRenderQueue` collects the frame’s draw requests:

1. `requests` stores the incoming model draws.
2. `transforms` and `colors` keep the per-draw payloads.
3. `sort_items` stores submesh sort/group records.
4. `instance_data` stores the resolved draw instances.
5. `indirect_commands` stores the final `vkCmdDrawIndirect` commands.
6. `instance_slice` and `indirect_slice` are the GPU upload destinations.

## 3. Initialization path

The pipeline starts in `model_api_init(max_models, instance_capacity)`.

The initialization flow is:

1. Store a pointer to the global renderer.
2. Initialize the asset system with storage for models, materials, submeshes, and clips.
3. Initialize the render queue with request capacity and indirect capacity.
4. Initialize the instance system with the requested instance capacity.
5. Create the shared compute skinning pipeline from `compiledshaders/skinning.comp.spv`.

This matters because the model pipeline assumes the renderer already owns:

1. A shared bindless descriptor set.
2. A shared pipeline layout.
3. A GPU pool for buffer slices.
4. A command buffer path that supports compute and graphics in the same frame.

## 4. Asset loading path

### 4.1 Loading entry points

The public entry points are:

1. `model_api_load_meshx`
2. `model_api_find_or_load_meshx`
3. `model_api_load_gltf`
4. `model_api_find_or_load_gltf`

The `find_or_load` variants first check whether the model already exists in the asset system and return the existing handle if possible.

### 4.2 Mesh import

For MeshX input, `model_source_from_meshx` parses the text asset into a temporary `ModelSource`.

The parser collects:

1. Positions.
2. UVs.
3. Optional normals.
4. Optional tangents.
5. Optional joints and weights.
6. Indices.
7. Materials, clips, submeshes, and bounds.

The parser also records whether the vertex layout declared skinning-related attributes. If the source is skinned but missing normals or tangents, the loader can synthesize them before upload.

### 4.3 Model creation from source

`model_asset_create_from_source` converts the parsed source into runtime assets.

For each model, it:

1. Reserves a new model handle.
2. Copies material data into `MaterialData` and `MaterialGpu`.
3. Creates submesh entries.
4. Allocates GPU slices for positions, UVs, indices, and packed skinning data when needed.
5. Uploads the CPU source arrays into GPU memory via the pending upload path.
6. Stores device addresses for shader access.
7. Marks the asset tables dirty so they will be uploaded later in the frame.

The important model-side guarantee is that asset data becomes stable once the upload is complete. Instances then reuse those slices.

## 5. Instance creation path

`model_instance_create(model)` creates a runtime instance for an already-loaded model.

The current behavior is:

1. Allocate a new instance ID.
2. Zero the instance record.
3. Attach the model handle.
4. Initialize transform and color defaults.
5. Initialize animation state.
6. If the model uses skinning, allocate a skinned output buffer and a palette buffer.
7. Mark the instance active and add it to the active instance list.

For skinned models, the instance owns the per-instance output buffers used by the compute skinning pass.

That means the instance is the boundary for animation state and skinning output, while the model asset remains the boundary for shared geometry and materials.

## 6. Animation update path

The current animation update flow is split into two parts:

1. `animation_system_begin_frame(dt_sec)` stores the frame delta time.
2. `animation_system_update_instance(instance)` advances time for a single active instance.

The update logic is simple today:

1. Skip inactive, paused, or clipless instances.
2. Query clip duration from the model asset.
3. Advance the instance time by `dt * speed`.
4. Wrap or clamp based on looping mode.

The current code still uses a minimal playback model. It is enough for clip stepping, but not yet a full multi-layer blending graph.

## 7. Compute skinning path

The compute skinning work happens in `animation_system_prepare_frame(cmd)`.

The current sequence is:

1. Iterate over the active instance list.
2. Skip instances that are inactive or not marked dirty.
3. Skip models that do not have skinning enabled.
4. Count the total vertices across the model’s submeshes.
5. Build a palette buffer, currently filled with identity matrices as a placeholder.
6. Resolve the packed source vertex buffer address.
7. Resolve the skinned output buffer address.
8. Resolve the palette buffer address.
9. Bind the compute pipeline and shared descriptor set.
10. Push `SkinningPush` constants.
11. Dispatch compute workgroups.
12. Barrier the skinned output buffer for graphics reads.
13. Clear the instance’s dirty flag.

### 7.1 Why the packed vertex buffer matters

The compute shader expects a packed vertex stream containing:

1. Position.
2. Normal.
3. Tangent.
4. Joint indices.
5. Joint weights.

A position-only buffer is not enough. The compute shader reads the full vertex record, so the load path must provide the same layout that the shader expects.

### 7.2 Current skinning contract

`SkinningPush` carries three device addresses plus counts:

1. `src_vertex_ptr` for the packed source vertices.
2. `dst_vertex_ptr` for the skinned output buffer.
3. `palette_ptr` for joint matrices.
4. `vertex_count` and `joint_count` for bounds checking.

This is a direct data-flow contract. If any of those addresses point at the wrong slice, the skinning output becomes invalid.

## 8. Frame begin and queueing path

### 8.1 Begin frame

`model_api_begin_frame(cam)` resets the per-frame queue state.

It copies the view-projection matrix into the queue and clears the request, transform, color, sort, instance, and indirect counts.

This call is important because it defines the boundary between the previous frame and the new frame.

### 8.2 Queueing draws

`model_api_draw(model, model_matrix, color)` does not draw immediately.

Instead, it:

1. Validates the model handle.
2. Appends a `DrawRequest`.
3. Copies the model matrix into the queue’s transform array.
4. Copies the color into the queue’s color array.
5. Increments the request count.

`draw3d` and `draw_model` are convenience wrappers over the same queueing path.

That means the public model draw API is effectively a submission list, not an immediate-mode draw call.

## 9. Prepare-frame path

`model_api_prepare_frame(cmd)` is the bridge between CPU queueing and GPU submission.

It performs three main jobs:

1. Flush pending asset uploads.
2. Build draw sort/group data.
3. Upload the resolved instance and indirect command arrays.

### 9.1 Pending uploads

The asset system owns `pending_uploads`. During prepare-frame, each upload request is replayed into a GPU slice.

After each upload, the code inserts a barrier so shaders can safely read the new data.

### 9.2 Sort and grouping

For each queued model request, the system expands the model into its submeshes and creates one sort item per submesh.

The sort key currently groups mainly by material and submesh identity. This produces a more coherent indirect list and reduces pipeline churn.

### 9.3 Instance and indirect build

The render queue then resolves each sort item into a `ModelInstanceGpu` record.

For each resolved draw:

1. The model handle is copied.
2. The submesh ID is stored.
3. The material ID is copied from the submesh.
4. The transform and color are copied.
5. If a matching active skinned instance exists, the queue may override the position pointer with the skinned output buffer.

After that, the queue builds indirect draw commands by grouping consecutive resolved instances that share the same submesh and material.

## 10. Draw submission path

`model_api_draw_queued(cmd)` consumes the prepared queue and emits the GPU draw.

The current flow is:

1. Skip if the queue was not prepared or contains no work.
2. Build `GltfIndirectPush`.
3. Bind viewport and scissor.
4. Bind the shared graphics pipeline.
5. Push constants containing device addresses and the frame view-projection matrix.
6. Issue one `vkCmdDrawIndirect` call over the indirect command buffer.

The shader side then resolves:

1. Submesh metadata.
2. Material metadata.
3. Per-instance data.
4. Optionally, skinned position overrides.

That keeps the draw call count low and moves most of the per-draw state into GPU-accessible tables.

## 11. Frame-level execution order

The current runtime order is roughly:

1. `model_api_begin_frame(cam)`
2. Queue model draws with `model_api_draw`, `draw3d`, or `draw_model`
3. `animation_system_begin_frame(dt)`
4. `animation_system_update_instance` for animated instances
5. `animation_system_prepare_frame(cmd)`
6. `model_api_prepare_frame(cmd)`
7. `model_api_draw_queued(cmd)`

The current implementation may interleave the animation and model steps differently depending on where the frame code calls them, but the data dependencies are the same.

## 12. What is broken today

The model pipeline works by contract, not by magic. When it breaks, it is usually because one of these contracts is violated:

1. A shader expects packed data and the loader gives a split stream.
2. A draw path assumes an instance output buffer exists and it does not.
3. Uploads are not flushed before the GPU reads them.
4. Skinning data is marked dirty but not rebuilt.
5. The render queue uses stale or mismatched submesh data.

The geometry distortion issue came from the first category: the compute skinning path was not reading the same vertex layout that the loader actually produced.

## 13. Practical mental model

A useful way to think about the current pipeline is:

1. Models are shared assets.
2. Instances are per-frame runtime state.
3. Animation produces updated state for skinned instances.
4. Skinning writes an instance-local output buffer.
5. The render queue turns requests into grouped indirect draws.
6. The shader reads tables and addresses rather than bound vertex buffers.

If you keep those ownership boundaries clear, the existing pipeline is easy to extend without adding a second renderer inside the renderer.

## 14. Immediate next improvements

The next incremental improvements should be:

1. Make the packed skinning buffer generation fully explicit in the import path.
2. Move instance state toward hot/cold storage.
3. Replace repeated linear scans with dense sparse-set iteration.
4. Reduce per-upload heap churn with a frame upload arena.
5. Split asset, instance, animation, and render logic into separate source files when the current behavior is stable.

These changes do not need to happen all at once. The important part is to keep each system’s data boundary explicit.
