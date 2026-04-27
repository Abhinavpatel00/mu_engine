# High Performance Animation System Design

Status: Proposed  
Date: 2026-04-27  
Applies to: MeshX v2 pipeline, model submission path, bindless renderer

## 1. Why This Document Exists

You already have a strong renderer foundation. The animation system should match that standard:

1. Fast at scale.
2. Predictable frame cost.
3. Data-oriented, not object-oriented ceremony.
4. Compatible with your bindless, indirect-draw model.
5. Easy to reason about when debugging.

This document defines the full animation stack from file loading to final skinning output.

## 2. Performance Targets

Define targets first so design choices stay grounded.

## 2.1 Baseline targets

1. 200 animated characters on desktop at stable frame time.
2. Animation update cost bounded by visible actors and update tier.
3. No per-character dynamic allocations during gameplay.
4. No expensive per-frame parser work.
5. Deterministic update order and reproducible results.

## 2.2 Budget model

Per frame animation budget should be explicit, for example:

1. Sampling and blending: 0.8 ms.
2. Pose evaluation and hierarchy solve: 0.6 ms.
3. Skinning upload or compute dispatch: 0.8 ms.
4. Graph/state machine logic: 0.3 ms.

Total animation budget target: about 2.5 ms on target hardware.

## 3. End-to-End Data Flow

The system is mostly data routing and transform math.

```text
MeshX / MeshXB
  -> parse and validate
  -> bake runtime clips and skeletons
  -> create animation runtime assets
  -> per-frame state machine update
  -> sample clip channels
  -> blend local pose
  -> solve global pose
  -> build skin matrices or dual quaternions
  -> GPU skinning or CPU skinning
  -> render indirect
```

Keep each stage explicit and measurable.

## 4. Core Runtime Model

Split responsibilities to avoid the future junk drawer problem.

## 4.1 Asset layer

Owns immutable data:

1. Skeleton topology.
2. Inverse bind matrices.
3. Compressed or raw clip tracks.
4. Curve metadata.
5. Optional events and root motion curves.

## 4.2 Instance layer

Owns per-entity mutable state:

1. Current clip and time.
2. Blend weights.
3. Playback flags.
4. Pose buffers.
5. Dirty flags and update tier.

## 4.3 Submission layer

Consumes final skinning results and binds to render path:

1. Buffer addresses or IDs for per-instance skinning data.
2. Draw packet integration for indirect rendering.
3. No gameplay logic inside this layer.

## 5. Math Foundations

## 5.1 Transform composition

A joint local transform is TRS:

$$
T_{local} = T(\mathbf{t}) \cdot R(\mathbf{q}) \cdot S(\mathbf{s})
$$

Global transform from hierarchy:

$$
T_{global}(j) = T_{global}(parent(j)) \cdot T_{local}(j)
$$

Root joint has:

$$
T_{global}(root) = T_{local}(root)
$$

## 5.2 Linear blend skinning

For vertex position $\mathbf{p}$ and up to 4 joint influences:

$$
\mathbf{p}' = \sum_{i=0}^{3} w_i \cdot \left(M_{skin}(j_i) \cdot \mathbf{p}_h\right)
$$

where:

$$
M_{skin}(j) = T_{global}(j) \cdot B^{-1}(j)
$$

$B^{-1}$ is inverse bind matrix. Weights satisfy:

$$
\sum_i w_i = 1, \quad w_i \ge 0
$$

## 5.3 Normal transform

For non-uniform scale, normal should use inverse transpose:

$$
\mathbf{n}' = normalize\left((M^{-1})^T \mathbf{n}\right)
$$

In practice for skinning, either:

1. Compute skinned normal with matrix blend approximation.
2. Or use dual quaternion skinning path to reduce artifacts.

## 5.4 Quaternion interpolation

For rotation keys $q_0$ and $q_1$ at normalized time $t$:

$$
q(t) = slerp(q_0, q_1, t)
$$

If dot product is negative, flip one quaternion before interpolation to keep shortest arc.

## 5.5 Dual quaternion skinning option

Dual quaternion skinning reduces candy-wrapper artifacts on twisting limbs.

If implemented, skinning blend is done in dual quaternion space, then converted to matrix for vertex transform. Keep this as optional path if your first milestone is LBS.

## 6. Animation Data Layout for Cache Efficiency

Data layout matters more than many algorithmic micro-optimizations.

## 6.1 Track storage

Use structure-of-arrays style:

1. Times in one contiguous array per channel.
2. Values in tightly packed arrays by channel type.
3. Channel metadata table points to ranges.

Example channel metadata:

1. target_joint.
2. path: translation, rotation, scale, weights.
3. interpolation mode.
4. key_offset.
5. key_count.

## 6.2 Pose storage

Separate local and global pose arrays:

1. local_t array of vec3.
2. local_r array of quat.
3. local_s array of vec3.
4. global_m array of mat4 or affine 3x4.

For large crowds, store affine 3x4 instead of full 4x4 where possible.

## 6.3 Alignment and SIMD

1. Keep quaternions aligned to 16 bytes.
2. Process joints in SIMD-friendly chunks.
3. Minimize branchy per-joint logic in hot loops.

## 7. Loading Pipeline Design

## 7.1 Parse stage

From MeshX sections:

1. skeletons.
2. skins.
3. animations.
4. morph_targets optional.

Parser output should be plain intermediate structs with strict validation.

## 7.2 Validation stage

Hard fail on:

1. Joint index out of range.
2. Key times not strictly ascending.
3. Channel path value arity mismatch.
4. Skin joint count mismatch.
5. Invalid quaternion magnitudes if beyond epsilon.

Soft warn on:

1. Missing optional channels.
2. Non-critical metadata omissions.

## 7.3 Build stage

Convert parsed data into runtime-optimized blobs:

1. Build parent index array.
2. Build topological order.
3. Build per-clip channel lookup tables.
4. Precompute clip bounds and duration reciprocals.
5. Optionally pre-quantize tracks.

## 7.4 Compression stage

Short term:

1. Keep raw keyframes.

Mid term:

1. Add optional compressed streams with codec tag.
2. Support ACL or equivalent transform compression.

Critical rule: decode path must be deterministic and versioned.

## 8. Runtime Evaluation Pipeline

## 8.1 Per-frame order

1. Advance playback times.
2. Evaluate state machine and transitions.
3. Sample source clips.
4. Blend local pose.
5. Solve hierarchy to global pose.
6. Build skinning transforms.
7. Submit GPU skinning inputs.

## 8.2 Sampling algorithm

For each channel:

1. Find keyframe interval by cached cursor or binary search.
2. Compute normalized segment time.
3. Interpolate value by mode.
4. Write to local pose component.

Optimization:

1. Use key cursor coherence between frames.
2. Most channels move forward monotonically in time.

## 8.3 Blending

Use layered blend model:

1. Base locomotion layer.
2. Additive upper-body layer.
3. Optional override layers by mask.

Translation and scale blend linearly.
Rotation blend using normalized quaternion blend or slerp depending on quality tier.

## 8.4 Hierarchy solve

Topological parent-first loop:

1. root to leaf multiplication.
2. optional skip for joints unaffected by masks if using partial solves.

## 9. Skinning Execution Strategy

## 9.1 Preferred default: GPU skinning

Given your renderer model, GPU skinning aligns best.

Approach:

1. Per-instance joint palette buffer (mat4 or affine 3x4).
2. Shader fetches joint matrices by instance ID and joint indices.
3. No per-draw vertex/index rebind overhead.

## 9.2 Two GPU paths

1. Vertex shader skinning:
   1. Simpler pipeline integration.
   2. Cost paid each raster pass.
2. Compute pre-skinning:
   1. Skin once to intermediate buffers.
   2. Better when reused by multiple passes.

Use vertex path first for simplicity, then add compute path for heavy scenes.

## 9.3 CPU skinning fallback

Keep as compatibility path only:

1. Useful for debug and very low-end fallback.
2. Avoid as default for modern content scale.

## 10. Update Rate and LOD Strategy

Not every animated actor needs full-rate updates.

Tiered strategy:

1. Tier 0: on-screen hero, full rate every frame.
2. Tier 1: near crowd, half rate with interpolation.
3. Tier 2: far crowd, quarter rate and simplified graph.
4. Tier 3: off-screen, pose freeze or very low frequency.

For crowd scenes this is often the single biggest win.

## 11. Root Motion and Gameplay Sync

Root motion extraction should be explicit.

## 11.1 Root motion curve

For clip root transform $R(t)$, frame delta is:

$$
\Delta R = R(t + \Delta t) \cdot R(t)^{-1}
$$

Apply gameplay policy:

1. Full root motion.
2. Planar only.
3. Rotation only.
4. Disabled.

## 11.2 Network and determinism

1. Use fixed tick for authoritative simulation.
2. Keep render interpolation separate from simulation state.
3. Record clip id, normalized time, and blend weights for replay.

## 12. Animation Graph Architecture

Keep graph practical and compact.

## 12.1 Nodes

1. Clip node.
2. Blend node 1D and 2D.
3. Additive node.
4. State machine node.

## 12.2 Transitions

1. Condition-based.
2. Duration.
3. Exit-time support.
4. Optional custom blend curve.

## 12.3 Runtime storage

Compile graph to flat arrays:

1. Node table.
2. Transition table.
3. Parameter table.
4. Constant pool.

Avoid pointer-heavy graph execution at runtime.

## 13. Job System and Parallelism

Animation is naturally parallel by instance.

## 13.1 Job granularity

1. Batch instances by skeleton type.
2. One job evaluates N instances.
3. Separate sampling and hierarchy solve jobs if needed.

## 13.2 Avoid false sharing

1. Per-job scratch buffers.
2. SoA pose outputs.
3. Align writable ranges to cache lines.

## 14. Memory Budgeting

Track memory by category:

1. Skeleton topology.
2. Clip keyframes compressed and raw.
3. Per-instance state.
4. Per-frame temporary pose buffers.
5. GPU joint palette buffers.

Use fixed pools where possible. Avoid runtime heap churn.

## 15. Integrating With Current Renderer

Your renderer already favors bindless IDs, device addresses, and indirect draws. Animation should plug into that contract.

## 15.1 Proposed integration points

1. Extend model asset data with animation handle and skeleton handle.
2. Add per-instance animation state table parallel to draw request table.
3. Upload joint palette slices through existing upload path.
4. Pass joint palette base address or index through push constants.
5. Keep render and animation ownership split:
   1. Animation system writes palette outputs.
   2. Model submission consumes outputs.

## 15.2 Frame execution placement

Recommended order:

1. frame_start.
2. animation_begin_frame.
3. animation_update_and_sample.
4. animation_build_skinning_data.
5. model_api_prepare_frame.
6. model_api_draw_queued.
7. post chain and submit.

## 16. API Sketch

High-level API proposal:

1. animation_system_init.
2. animation_system_shutdown.
3. animation_register_model_rig.
4. animation_create_instance.
5. animation_set_state_params.
6. animation_update_frame.
7. animation_get_palette_gpu_slice.

Example C-style signatures:

1. bool animation_system_init(uint32_t max_rigs, uint32_t max_instances)
2. bool animation_register_rig(const RigAssetDesc* desc, RigHandle* out_rig)
3. bool animation_create_instance(RigHandle rig, AnimInstanceHandle* out_inst)
4. void animation_update_frame(const AnimationFrameContext* ctx)
5. bool animation_get_instance_palette(AnimInstanceHandle inst, BufferSlice* out_slice)

## 17. Debugging and Tooling

Build tools early to prevent blind tuning.

## 17.1 Runtime debug views

1. Skeleton overlay.
2. Joint heatmap by update cost.
3. Clip timeline and active transitions.
4. Root motion trace.

## 17.2 Metrics

1. Characters updated per tier.
2. Sampling time.
3. Hierarchy solve time.
4. Skinning upload time.
5. GPU skinning pass cost.

## 17.3 Validation tools

1. Offline validator for clip continuity.
2. Quaternion normalization checks.
3. NaN guards in runtime hot loops.

## 18. Rollout Plan

## Phase 1: Functional baseline

1. Load skeleton, skin, and raw clips.
2. Single clip playback.
3. CPU pose solve.
4. Vertex shader skinning.

## Phase 2: Production runtime

1. State machine transitions.
2. Layered blend with masks.
3. Root motion extraction.
4. Per-instance update tiers.

## Phase 3: Performance upgrades

1. Track compression decode path.
2. SIMD pose solve.
3. Compute skinning optional path.
4. Crowd optimization and culling tie-ins.

## Phase 4: Advanced features

1. Additive aim and recoil layers.
2. Morph target animation support.
3. Event tracks integrated with gameplay.
4. Streaming clips and partial resident sets.

## 19. Common Failure Modes and Preventive Rules

1. Failure: graph logic inside renderer.
   Rule: animation system owns playback logic, renderer consumes outputs.
2. Failure: per-frame allocations.
   Rule: all runtime buffers are preallocated pools.
3. Failure: hidden coordinate conventions.
   Rule: enforce unit and axis conversion once in import pipeline.
4. Failure: too many high-rate updates.
   Rule: always apply update tiers and visibility policy.
5. Failure: desync between simulation and render time.
   Rule: fixed tick simulation with render interpolation.

## 20. Final Recommendations

1. Start with clean baseline LBS plus robust state machine.
2. Optimize data layout before adding feature complexity.
3. Keep ownership boundaries strict:
   1. Asset loading.
   2. Animation playback.
   3. Render submission.
4. Measure each stage separately.
5. Add compression and compute skinning only after baseline metrics are stable.

If you follow this plan, animation performance will scale with your renderer architecture instead of fighting it.
