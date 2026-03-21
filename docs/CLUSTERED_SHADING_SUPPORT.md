# Clustered Shading Support Plan

## Goal
Add clustered shading so each shaded pixel evaluates only the lights affecting its 3D screen-space cluster, instead of scanning all lights.

This plan is designed for the current renderer constraints:
- Single descriptor set layout (bindless model)
- Single pipeline layout for graphics + compute
- GPU buffers suballocated from big pools with offset allocator
- Offsets/ranges passed via push constants

## Scope
### In Scope
- Point/spot light assignment to clusters
- Compute-based cluster build and light assignment
- Forward/fragment cluster lookup for lighting
- Resource layout compatible with existing bindless system

### Out of Scope (initial)
- Shadow map integration
- Decal/probe clustering
- Tiled deferred fallback path

## High-Level Pipeline
1. Upload/update dynamic light list (`LightGPU[]`)
2. Build cluster metadata (AABB or plane params per cluster)
3. Reset per-cluster counters
4. Assign lights to clusters (compute)
5. Shade using per-pixel cluster lookup in graphics pass

## Cluster Layout
Use a fixed 3D grid in view space:
- `cluster_x = ceil(render_width / tile_size_x)`
- `cluster_y = ceil(render_height / tile_size_y)`
- `cluster_z = z_slice_count` (logarithmic depth slicing)

Recommended starting values:
- `tile_size_x = 16`, `tile_size_y = 16`
- `z_slice_count = 24`

Total clusters:
- `cluster_count = cluster_x * cluster_y * cluster_z`

## GPU Data Structures
All buffers are SSBO-backed and bindless-visible.

```c
// 16-byte aligned fields preferred for std430
struct LightGPU {
    float position_radius[4]; // xyz + radius
    float color_intensity[4]; // rgb + intensity
    float direction_type[4];  // xyz + type (0=point,1=spot)
    float spot_params[4];     // cosInner, cosOuter, unused, unused
};

struct ClusterHeader {
    uint32_t offset; // start in global light-index list
    uint32_t count;  // number of lights for this cluster
    uint32_t pad0;
    uint32_t pad1;
};
```

Required buffers:
- `lights_buffer`: array of `LightGPU`
- `cluster_headers_buffer`: array of `ClusterHeader` (`cluster_count` entries)
- `cluster_light_indices_buffer`: flat list of light indices
- `cluster_counters_buffer`: temporary atomic counters per cluster
- optional `cluster_debug_buffer`

## Memory Budgeting
Define:
- `max_lights`
- `max_lights_per_cluster` (hard cap)

Then:
- `cluster_headers_buffer_size = cluster_count * sizeof(ClusterHeader)`
- `cluster_counters_buffer_size = cluster_count * sizeof(uint32_t)`
- `cluster_light_indices_buffer_size = cluster_count * max_lights_per_cluster * sizeof(uint32_t)`

Start conservative:
- `max_lights = 4096`
- `max_lights_per_cluster = 128`

If overmu occurs, clamp writes and track overmu count for debugging.

## Compute Passes
## 1) Reset Counters
Compute shader clears `cluster_headers.count` and temp counters.

## 2) Assign Lights to Clusters
For each light (1 thread = 1 light):
- Compute affected cluster bounds `(x0..x1, y0..y1, z0..z1)`
- Iterate candidate clusters
- Atomic increment cluster counter
- Write light index to flat `cluster_light_indices_buffer` if within cap

After assignment:
- Write each cluster header (`offset`, `count`) using fixed-stride layout:
  - `offset = cluster_id * max_lights_per_cluster`
  - `count = min(counter, max_lights_per_cluster)`

This avoids prefix-sum in v1.

## Shading Path Changes
In the lighting fragment shader:
1. Reconstruct view-space position/depth
2. Compute `cluster_id` from pixel `(x,y,z)`
3. Read `ClusterHeader`
4. Loop `i in [0, count)`:
   - `light_index = cluster_light_indices[offset + i]`
   - Fetch `LightGPU` and accumulate BRDF/lambert

## Bindless + Push Constant Contract
Do not add extra descriptor set layouts.
Use the existing bindless set and pass IDs/offsets via push constants.

Suggested push constants (extend existing render push block):
- `lights_buffer_id`
- `cluster_headers_buffer_id`
- `cluster_indices_buffer_id`
- `cluster_count_x`, `cluster_count_y`, `cluster_count_z`
- `tile_size_x`, `tile_size_y`
- `z_near`, `z_far`, `log_z_scale`, `log_z_bias`
- optional byte offsets if multiple arrays share one large buffer slice

## Synchronization
Required barriers:
- After reset pass -> before assignment pass: compute write -> compute read/write
- After assignment pass -> before graphics shading: compute write -> fragment read

Use `vkCmdPipelineBarrier2` with:
- src stage: `VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT`
- dst stage: `VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT` or `VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT`
- access masks: `VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT` to `VK_ACCESS_2_SHADER_STORAGE_READ_BIT`

## Integration Points in This Repo
- Add cluster/light buffer allocations near other per-frame buffers
- Add clustered compute pipelines to the pipeline cache/dirty-rebuild path
- Dispatch clustered compute passes each frame before main lighting draw
- Extend shading shader(s) to consume cluster buffers
- Keep shader filenames stable so hot-reload remains predictable

## Shader Set (Suggested)
- `shaders/cluster_reset.comp.slang`
- `shaders/cluster_assign.comp.slang`
- Update main lighting shader (fragment or compute-lighting path)

## Hot Reload Compatibility
For reliable pipeline hot reload:
- Ensure both graphics stages are tracked (vert + frag)
- Ensure compute stage path is tracked
- Keep one shader file per stage entrypoint where possible

## Rollout Plan
1. **Data plumbing only**: allocate buffers, upload test lights, visualize cluster IDs
2. **Assignment pass**: fill cluster lists, add debug counters
3. **Shading integration**: consume lists in fragment shader
4. **Optimization**: tighten bounds tests, reduce atomics, add overmu diagnostics

## Validation Checklist
- Cluster count matches current render extent each resize
- No out-of-bounds writes in cluster index buffer
- Overmu counter remains near zero in normal scenes
- Lighting result matches brute-force reference for small scenes
- GPU time improves as light count increases

## Future Extensions
- Clustered shadows (per-cluster shadow caster lists)
- Light-type bins (point/spot/separate lists)
- Prefix-sum compaction path for better memory usage
- Temporal reuse of cluster assignment for static lights
