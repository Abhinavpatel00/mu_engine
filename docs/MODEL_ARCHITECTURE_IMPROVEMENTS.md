# Model Architecture Improvements & Efficient Data Flow

Status: draft

This note summarizes recommended structural refactors and an immediate diagnosis of the rendering bug related to compute skinning and packed vertex layout. It also sketches an efficient data flow for animation -> skinning -> rendering.

---

## Immediate rendering problem (observed)
- The compute skinning shader expects a packed `SkinVertex` stream (position, normal, tangent, joints, weights) by device address.
- The existing importer uploaded separate `position`/`uv` slices and the runtime passed the position buffer address to the shader. The shader therefore read garbage for normals/joints/weights, producing distorted geometry and broken texturing.
- Quick mitigation: ensure the runtime passes a packed vertex buffer device address to the skinning shader (done in the current patch). Rebuild shaders (`./compileslang.sh`) and the project (`make`).

---

## Proposed module split (high level)
Split responsibilities by lifecycle and surface area:

- `model_assets.c` / `model_assets.h` — Import/parsing, ownership, GPU lifetime, single place for uploads.
- `model_instances.c` / `model_instances.h` — Instance handles, hot/cold split (hot small arrays for animation, cold arrays for GPU handles), sparse-set management.
- `animation_system.c` / `animation_system.h` — Clips, layers, sampling, pose blending, palette generation (produce GPU palette arena offsets or device addresses).
- `model_render.c` / `model_render.h` — Queueing, sorting, indirect build, GPU-side cull hooks, render submission.

Advantages: clear ownership, smaller files, easier testing, fewer accidental dependencies.

---

## Hot/Cold instance layout (recommended)
- Separate frequently-updated small arrays (anim time, speed, flags, dirty bit) from render-facing data (transform, color) and from heavy/cold data (BufferSlice handles).
- Use SoA where it benefits per-frame updates; keep compact hot arrays for SIMD/cache efficiency.

Example split:
- `instance_hot[]` — model_id, anim_time, speed, weight, palette_dirty
- `instance_render[]` — transform, color
- `instance_skin[]` — skinned buffer slice, palette slice

---

## Dense iteration: sparse-set for active instances
Replace linear scans and ad-hoc `active_instance_ids` manipulation with a proper sparse-set:
- O(1) add/remove, dense iteration for frame-update loops, compact memory layout.

---

## Upload system: frame upload arena
Current per-upload `malloc` copies are expensive. Use a linear frame upload arena:
- Single large CPU staging buffer per frame
- Upload requests push offsets into that arena, not heap allocations
- Playback copies from arena into mapped staging buffer and issues device-side copies
- Reset arena each frame

This avoids per-upload malloc churn and improves scheduler locality.

---

## Geometry layout: packed geometry block
Instead of many small slices per submesh, allocate one geometry block per submesh (or a single large block with offsets):
- [positions][uvs][indices][optional packed_skin_stream]
- Better locality, fewer buffer allocations, fewer descriptors/device addresses to manage.

---

## Skinning: batch and transient arenas
- Prefer transient (per-frame) skinned output arenas instead of persistent per-instance buffers unless caching is necessary.
- Batch many instances' palettes and vertex ranges into a small number of compute dispatches to reduce dispatch and barrier overhead.
- Record per-instance dispatch ranges and write offsets into the transient arenas.

---

## Animation system: layers and explicit blend state
- Replace toy `AnimationState` with small explicit layers and blend state (clip, target, blend_t, duration, speed, flags).
- Make blending explicit and observable for testing.

---

## Immediate next steps (practical)
1. Finish the design doc and identify the smallest incremental refactors.
2. Implement a frame upload arena (low-risk, large payoff).
3. Convert submesh allocation to packed geometry block.
4. Introduce sparse-set instance container and hot/cold split.
5. Batch skinning into transient arenas and change dispatch to ranges.

---

## Files changed so far
- Small targeted changes applied to `gltf_gpu_mesh.c` to produce packed vertex uploads and use packed buffer addresses for skinning.
- Helpers added in `gltf_gpu_mesh_helpers.c` to compute normals/tangents and pack skin vertices.

See: [docs/MODEL_ARCHITECTURE_IMPROVEMENTS.md](docs/MODEL_ARCHITECTURE_IMPROVEMENTS.md)

---

If you'd like, I can:
- Implement the frame upload arena next (I'll patch the upload API and replay code), or
- Start splitting `gltf_gpu_mesh.c` into the four modules above (I'll scaffold files and move code incrementally), or
- Draft a migration plan that sequences these changes to minimize churn.

Which action should I take next? (I recommend the frame upload arena first.)
