# Absolute Minimal Voxel + Clustered-Settings Experiment

## Goal
Render voxels with the smallest possible Vulkan path, using `vkCmdDrawIndexedIndirectCount`, so clustered-light parameters can be experimented with immediately.

This is intentionally **not** a full renderer architecture. It is a narrow MVP loop.

## Hard MVP Rules
- One voxel chunk (or a few fixed chunks), meshed on CPU.
- One graphics pipeline for voxel draw.
- One indirect draw command (`VkDrawIndexedIndirectCommand`) + one draw-count uint.
- No `vkCmdBindVertexBuffers` path; vertex/index data is accessed from pooled buffer slices using push-constant offsets.
- No streaming system, no LOD, no culling compute pass in v1.
- Clustered-light experimentation can start with **CPU-built cluster buffers** first.

## Why this is the minimum
`vkCmdDrawIndexedIndirectCount` gives a GPU-driven-compatible draw path now, while still allowing a single command setup.
You can keep draw submission stable and change only clustered data/logic.

## Minimal Resources
## Required GPU Buffers
1. One large geometry pool buffer (suballocated slices)
   - vertex slice (`VoxelVertex[]`)
   - index slice (`uint32_t[]`)
2. Indirect command buffer (`VkDrawIndexedIndirectCommand[1]`)
3. Draw count buffer (`uint32_t[1]`)
4. Camera + geometry-slice push constants
5. (Cluster test) Light + cluster buffers (can be small fixed-size)

## Required Images
- Color target (swapchain path already exists)
- Depth target (optional if you accept painter-like ordering; recommended to keep)

## Minimal Shader Set
- `voxel.slang` OR a single existing voxel pipeline already in repo.
- No separate compute passes required for first clustered test if cluster lists are CPU-built.

## Frame Flow (Absolute Minimal)
1. Update camera matrices.
2. If voxel chunk changed: rebuild mesh on CPU, upload into geometry pool slices.
3. Write one `VkDrawIndexedIndirectCommand`:
   - `indexCount = ib_count`
   - `instanceCount = 1`
   - `firstIndex = index_slice_first_index` (derived from slice byte offset)
   - `vertexOffset = vertex_slice_base_vertex`
   - `firstInstance = 0`
4. Write draw count = `1`.
5. (Cluster experiment path) rebuild tiny cluster/light buffers (CPU) and upload.
6. Record command buffer:
   - transitions
   - begin rendering
   - bind pipeline
   - push constants (camera + geometry slice offsets + cluster tuning params)
   - call `vkCmdDrawIndexedIndirectCount`
   - end rendering
7. Submit + present.

## Required Vulkan Call Sequence (inside cmd recording)
1. `vkCmdBindPipeline(..., VK_PIPELINE_BIND_POINT_GRAPHICS, voxelPipeline)`
2. `vkCmdDrawIndexedIndirectCount(...)`

Use:
- `buffer = indirect_cmd_buffer`
- `offset = indirect_cmd_offset`
- `countBuffer = draw_count_buffer`
- `countBufferOffset = draw_count_offset`
- `maxDrawCount = 1`
- `stride = sizeof(VkDrawIndexedIndirectCommand)`

## Geometry Slice Contract (No VB/IB Binding Model)
- Keep vertex and index arrays in suballocated ranges of one large GPU buffer.
- Pass slice offsets through push constants (or draw data) to the shader.
- Vertex shader performs vertex pulling from storage buffer using those offsets.
- `vkCmdDrawIndexedIndirectCount` still consumes index stream state, so bind the same pooled backing buffer once as the index source.
- You are not creating per-mesh dedicated Vulkan vertex/index buffer objects; you are slicing one pooled buffer.

## Minimal Clustered Experiment Modes
Start with toggles rather than full system changes.

## Mode A (Fastest Bring-up)
- Build cluster assignment on CPU each frame (or when camera/lights move).
- Upload `ClusterHeader[]` and `ClusterLightIndex[]` to GPU.
- Fragment shader does clustered lookup.
- No compute pipeline yet.

## Mode B (Next step)
- Keep same shading path.
- Move assignment to a single compute pass.
- Draw path unchanged (`vkCmdDrawIndexedIndirectCount` remains identical).

This keeps your experiment stable: only cluster data producer changes.

## Push Constant MVP
Keep only what is needed:
- camera/view-proj
- `vertex_slice_byte_offset`
- `index_slice_byte_offset` (if shader/debug path needs it)
- `vertex_stride_bytes`
- cluster grid dims (`cx, cy, cz`)
- tile size (`tx, ty`)
- near/far and z-slice params
- bindless IDs (or offsets) for cluster/light buffers

## Suggested Minimal Limits
- `maxLights = 256`
- `tile = 16x16`
- `zSlices = 16`
- fixed `maxLightsPerCluster = 64`

These are enough to measure behavior without large memory pressure.

## What to skip for now
- GPU frustum culling
- occlusion culling
- chunk streaming
- per-material systems
- shadowed clustered lights
- transparent voxel lighting

## Success Criteria
You are done with the minimal path when:
1. Voxel chunk renders every frame through `vkCmdDrawIndexedIndirectCount`.
2. Setting draw count to 0 cleanly renders nothing (sanity check).
3. Cluster parameter changes (tile/z slices/light count) visibly change lighting cost/result.
4. No pipeline or descriptor layout changes are required when switching CPU vs compute cluster assignment.

## Practical Implementation Order
1. Lock one voxel pipeline and one chunk mesh.
2. Wire indirect command + draw count buffers and call `vkCmdDrawIndexedIndirectCount`.
3. Add tiny light array and brute-force shading baseline.
4. Add CPU cluster build + clustered lookup in shader.
5. Add timing/profiler markers for comparison.
6. Only then move clustering to compute.

## Bottom Line
The absolute minimal route is:
- **single voxel graphics pipeline**
- **single indirect indexed draw with count = 1**
- **cluster data built on CPU first**
- **same draw command path kept unchanged**

That gives immediate room to experiment with clustered settings while avoiding premature engine complexity.
