# Compute Skinning Implementation Guide

Status: Proposed  
Date: 2026-04-27  
Applies to: MeshX runtime, animation system, bindless Vulkan renderer

## 1. Purpose

Compute skinning moves the skinning work out of the vertex shader and into a compute pass.

That is useful when:

1. Many animated characters share the same skeleton evaluation cost.
2. The same skinned result is reused by more than one render pass.
3. The scene is heavy enough that per-vertex skinning in the graphics pipeline becomes a bottleneck.
4. You want clearer separation between animation evaluation and rendering.

In this engine, the main constraint is not just performance. It is architectural fit:

1. Keep the single bindless descriptor model.
2. Keep suballocated buffers and offset-based access.
3. Keep push constants small and aligned.
4. Avoid introducing a second ad hoc asset or rendering path.

## 2. What Compute Skinning Should Do

There are two practical compute-skinning models.

### 2.1 Skin joints, then skin vertices

This is the classic pipeline:

1. Evaluate animation clips into local poses.
2. Solve the skeleton hierarchy into global joint matrices.
3. Build skin matrices from global joints and inverse bind pose.
4. Dispatch a compute shader that reads the mesh source stream and writes skinned vertices into an output buffer.
5. The graphics pass reads the skinned output buffer.

This is the simplest to integrate if the renderer already consumes buffer slices by offset.

### 2.2 Skin only once per frame per instance

If multiple passes need the same animated geometry, do the skinning once and reuse the result.

That is the real win for compute:

1. Main color pass.
2. Shadow pass.
3. Depth prepass.
4. Optional velocity or debug pass.

Without compute, each pass tends to duplicate the skinning work.

## 3. Recommended Data Model

### 3.1 Per-instance runtime data

Each animated instance should have a compact runtime record containing:

1. Rig or skeleton handle.
2. Current clip/time state.
3. Palette buffer slice or device address.
4. Skinned output buffer slice.
5. Vertex and index slice references.
6. Dirty flags for animation and skinning.

Keep this data separate from the render submission record so animation changes do not spill into draw code.

### 3.2 Joint palette layout

Use a palette of skin matrices or affine transforms.

Recommended first pass:

1. `mat4` per joint for simplicity.
2. 16-byte alignment per joint matrix.
3. One palette per animated instance.

If memory pressure becomes high, move to affine 3x4 matrices later.

For each joint:

$$
M_{skin}(j) = T_{global}(j) \cdot B^{-1}(j)
$$

Where:

1. $T_{global}(j)$ is the solved global joint transform.
2. $B^{-1}(j)$ is the inverse bind matrix.

### 3.3 Vertex input layout

Skinning input needs at minimum:

1. Position.
2. Normal.
3. Tangent.
4. Joint indices.
5. Joint weights.

Suggested packed form:

```c
typedef struct SkinVertex
{
    float position[3];
    float normal[3];
    float tangent[4];
    uint16_t joints[4];
    uint16_t weights[4];
} SkinVertex;
```

If you already store weights as normalized integers in MeshX, decode them in compute.

### 3.4 Output buffer layout

The output buffer should be a tightly packed skinned stream.

Possible layouts:

1. Full transformed vertex stream.
2. Positions only, if normals are recomputed later.
3. Positions plus normals plus tangents for full material correctness.

For a normal-mapped renderer, the safest first version is:

1. skinned position.
2. skinned normal.
3. skinned tangent.

## 4. Math Model

### 4.1 Linear Blend Skinning

For a vertex position $\mathbf{p}$ and up to 4 influences:

$$
\mathbf{p}' = \sum_{i=0}^{3} w_i \cdot \left(M_{skin}(j_i) \cdot \mathbf{p}_h\right)
$$

Where:

1. $w_i$ are normalized weights.
2. $j_i$ are joint indices.
3. $\mathbf{p}_h$ is the homogeneous position.

### 4.2 Normal and tangent transform

Normals and tangents should be transformed by the rotation-scale part of the skin matrix.

If non-uniform scale is present, the mathematically correct path is inverse transpose for normals:

$$
\mathbf{n}' = normalize\left((M^{-1})^T \mathbf{n}\right)
$$

In practice, compute skinning often uses matrix blending approximations. That is acceptable if your content pipeline keeps rig scaling sane.

## 5. GPU Pipeline Shape

### 5.1 Frame order

The frame should run in this order:

1. Begin frame.
2. Update animation state.
3. Build joint palettes.
4. Dispatch compute skinning for dirty instances.
5. Barrier skinned output for graphics reads.
6. Submit draw passes.

### 5.2 Dispatch granularity

Two good dispatch options exist:

1. One compute dispatch per skinned instance.
2. One compute dispatch per mesh section or instance batch.

For your renderer, batching by instance is usually better because it matches existing indirect submission and keeps per-dispatch overhead low.

A common dispatch mapping is:

1. One workgroup per vertex batch.
2. Each invocation processes one vertex.
3. Larger meshes use multiple workgroups.

### 5.3 Example compute shader responsibilities

The compute shader should:

1. Fetch joint indices and weights.
2. Fetch palette matrices from a bindless buffer or device address.
3. Blend transformed position, normal, and tangent.
4. Write results to the skinned output buffer.

Keep branch logic minimal.

## 6. Integration With the Current Renderer

### 6.1 Descriptor model

Do not add a second descriptor system.

Keep the existing single bindless set layout and expose skinning resources through the same path used by the rest of the renderer.

That means skinning inputs should be represented as one of these:

1. Bindless buffer IDs.
2. Buffer device addresses.
3. Suballocated buffer slices passed via push constants.

### 6.2 Push constant policy

Push constants should remain routing data only:

1. Joint palette offset or device address.
2. Source vertex slice offset.
3. Destination vertex slice offset.
4. Vertex count.
5. Material or submesh identifier.

Do not pass large pose blobs through push constants.

### 6.3 Buffer ownership

Use suballocated buffers for:

1. Skinning input stream.
2. Skinned output stream.
3. Joint palette stream.
4. Optional temporary scratch.

This matches the current engine style and avoids adding per-instance GPU allocations.

## 7. Barriers and Synchronization

Compute skinning only works if the read/write hazards are explicit.

### 7.1 Required transitions

Typical sequence:

1. Animation writes palette buffer.
2. Compute shader reads palette and source vertices.
3. Compute shader writes skinned output buffer.
4. Graphics pass reads skinned output buffer.

That means you need at least two barrier points:

1. Before compute, to make animation writes visible.
2. Before graphics, to make compute writes visible.

### 7.2 Example hazard rule

If compute writes buffer slice `A` and graphics reads buffer slice `A`, the barrier must cover the exact slice range, not the whole buffer unless the whole buffer was touched.

That keeps synchronization cheap and predictable.

### 7.3 Suggested stage pairing

1. Animation upload or solve stage -> compute stage.
2. Compute stage -> vertex shader or indirect draw stage.

Use the smallest correct stage/access masks.

## 8. Runtime API Shape

A clean API boundary keeps the system from becoming a hidden one-off.

### 8.1 Asset-side API

Suggested asset functions:

1. `animation_register_rig`
2. `animation_register_clip`
3. `animation_register_skin`
4. `animation_build_palette`

### 8.2 Runtime-side API

Suggested runtime functions:

1. `skin_system_init`
2. `skin_system_begin_frame`
3. `skin_system_queue_instance`
4. `skin_system_dispatch`
5. `skin_system_apply_barriers`

### 8.3 Data returned to render path

The render side should only need:

1. Skinned output slice.
2. Joint palette slice or address.
3. Vertex count.
4. Submesh/material identifiers.

## 9. Implementation Steps In This Codebase

### Step 1: Extend MeshX runtime data

Add skeleton and skin handles to the model asset representation.

The model should know:

1. Which skeleton it uses.
2. Which skin palette it needs.
3. Whether the mesh should use static or skinned rendering.

### Step 2: Add skinning buffers to the frame allocator

Reserve one or more transient buffers for:

1. Joint palettes.
2. Skinned vertex output.
3. Optional dispatch argument buffers.

### Step 3: Add a compute pipeline

Create a dedicated compute pipeline for skinning.

Keep the shader narrow and data-driven:

1. One input struct.
2. One output struct.
3. One kernel per vertex.

### Step 4: Feed the compute pass from animation update

The animation system should write joint palettes first, then enqueue skinning work only for dirty visible instances.

### Step 5: Route draw submission to skinned output

The graphics path should use the skinned output buffer slice instead of the original mesh stream when a model is flagged as skinned.

### Step 6: Add barriers and validation

Verify:

1. Buffer offsets are 16-byte aligned where required.
2. Palette writes are visible before compute.
3. Skinned outputs are visible before rendering.
4. Skinned and static paths both produce correct results.

## 10. Performance Notes

### 10.1 When compute skinning wins

Compute skinning is best when:

1. The character count is high.
2. Multiple passes reuse the same deformed mesh.
3. The graphics pipeline is already busy.
4. You want consistent animation cost per frame.

### 10.2 When it does not help

It may not help when:

1. Only a few characters are visible.
2. The scene is fill-rate bound rather than vertex bound.
3. Skinning output is used only once.

For those cases, vertex shader skinning may be simpler and cheaper.

### 10.3 Good first optimization choices

1. Batch dispatches by skeleton type.
2. Avoid per-instance reallocations.
3. Skip skinning for culled or frozen instances.
4. Use smaller update tiers for distant actors.

## 11. Debugging Strategy

Compute skinning is easy to break silently, so build inspection tools early.

1. Visualize palette matrices per joint.
2. Toggle compute vs vertex skinning at runtime.
3. Compare skinned output against CPU reference on a test mesh.
4. Add bounds and NaN checks in debug builds.
5. Render joint axes or joint heatmaps when validating rigs.

A good sanity test is to run the same animated model through:

1. CPU skinning.
2. Vertex shader skinning.
3. Compute skinning.

Then compare vertex output within tolerance.

## 12. Recommended Rollout Plan

### Phase 1: Minimal working path

1. Support one skeleton.
2. Support one palette per instance.
3. Skin positions only.
4. Render a single pass.

### Phase 2: Production path

1. Skin normals and tangents.
2. Reuse skinned output across passes.
3. Add visibility and update-tier culling.
4. Add indirect draw integration.

### Phase 3: Full integration

1. Add morph target compatibility.
2. Add palette compression if needed.
3. Add GPU-driven instance selection.
4. Add profiling and debug views.

## 13. Common Mistakes

1. Writing skinned data back into the source mesh buffer.
   Keep source and output separate.
2. Using per-frame allocations for palettes or outputs.
   Preallocate and suballocate instead.
3. Forgetting barriers between compute and graphics.
   This usually looks like random deformation or one-frame-late output.
4. Passing bulk pose state through push constants.
   Use buffer slices or addresses.
5. Skinning hidden or culled actors every frame.
   Respect visibility and update tiers.

## 14. Bottom Line

If you add compute skinning in this engine, the clean version is:

1. Animation builds a joint palette.
2. Compute uses the palette to deform a source vertex stream into a skinned output stream.
3. Rendering consumes the output through the existing bindless, suballocated, indirect draw path.

That preserves the current architecture instead of fighting it, and it gives you a path to scale animated crowds without turning the graphics pipeline into the bottleneck.
