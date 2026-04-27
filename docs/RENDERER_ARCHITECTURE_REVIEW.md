# Renderer Architecture Review

Date: 2026-04-27  
Scope: Current Vulkan renderer architecture and near/mid-term structural risks

## Executive Summary

This renderer is strong where most Vulkan renderers fail.

You already have the hard parts mostly right:
- Stable global bindless contract.
- Single descriptor set + single pipeline layout model.
- Explicit transitions and barriers.
- Dynamic rendering and reverse-Z defaults.
- Suballocated memory pools.
- GPU-friendly model draw path using indirect draws and device-address routing.

The frame is legible and operationally honest. That is a major asset.

The main risks are not low-level Vulkan correctness. The real risks are structural: data ownership, system boundaries, frame-state cohesion, and dependency visibility as features grow.

## What Is Strong

### 1. Frame execution is explicit and readable

The current flow is clear and practical:

```c
/*
frame_start()
model_api_begin_frame()
vk_cmd_begin()
model_api_prepare_frame()
model_api_draw_queued()
post_pass()
pass_smaa()
pass_ldr_to_swapchain()
flush_barriers()
vk_cmd_end()
submit_frame()
*/
```

This is easy to reason about for:
- CPU cost.
- Synchronization.
- Ownership and lifetime.
- Regression debugging.

### 2. Bindless contract is stable

Binding slots are fixed and globally meaningful:
- 0: textures
- 1: samplers
- 2: storage images

That stability is more valuable than almost any abstraction layer. It enables future systems to integrate without descriptor churn or per-feature binding semantics.

### 3. Data routing model is modern

The renderer effectively routes:
- Asset metadata and CPU submission data.
- GPU buffers and suballocated slices.
- Bindless IDs and addresses.
- Push constants as routing metadata.
- Shader-side fetch and execution.

This is the correct direction for scaling feature complexity without CPU submission collapse.

### 4. Minimal ceremony, high practical throughput

No descriptor set soup. No per-material pipeline chaos. No abstraction theater. The system is intentionally explicit and that is currently a competitive advantage.

## What Is Fragile

### 1. Global access pressure

Current global entry points are convenient:

```c
extern Renderer renderer;
extern EnginePipelines pipelines;
```

But this can drift into ambient coupling where every subsystem reaches into global state and mutates shared infrastructure.

Risk pattern:
- Fast integration now.
- Hidden coupling later.
- High regression blast radius after growth.

### 2. Manual pass graph is disciplined but implicit

Current pass orchestration is explicit in code, but dependencies are mostly remembered, not declared.

Today this works because the pass set is manageable.
Later this becomes fragile when post variants, compositing branches, or extra intermediate targets are added.

### 3. Push constants can become a dumping ground

The fixed 256-byte push model is good discipline, but only if usage remains strict.

Push constants should remain:
- Draw-local.
- Small.
- Routing-oriented (IDs, offsets, addresses).

If they accumulate broad feature state, they become unstable and hard to maintain across shader/host evolution.

### 4. Model API currently spans too many responsibilities

The model path does useful work, but currently mixes concerns:
- Asset lookup/load.
- Submission queueing.
- Frame prep/upload.
- Draw execution.

This is efficient now, but risky once animation, LOD, visibility tiers, streaming, and skinning complexity increase.

### 5. Frame state is distributed

Frame-critical state is currently spread across globals, frame contexts, camera, model queues, and pass assumptions.

This increases cognitive load and makes future integration less deterministic.

## What Will Hurt in 6 Months

Most likely failure modes:
1. Hidden pass dependencies causing intermittent hazards after adding new effects.
2. Growing subsystem coupling through direct global renderer access.
3. Model API becoming a junk drawer for scene + submission + rendering concerns.
4. Push constant scope creep leading to host/shader mismatch churn.
5. Frame ownership becoming procedural folklore instead of explicit data.

## Priority Actions (Highest Leverage)

### 1. Add formal pass contracts (now)

Do not build a full render graph yet.
Add per-pass declarations that state read/write/stage usage.

Example:

```yaml
pass: post_pass
reads:
  - hdr_color
writes:
  - ldr_color
stages:
  - compute
```

Goal: dependency visibility and maintainability, not automation.

### 2. Introduce a FramePacket (now)

Create a frame-scoped structure that holds all pass-relevant state in one place.

Example shape:

```c
typedef struct FramePacket {
    uint32_t frame_index;
    float dt;
    Camera* camera;
    VkCommandBuffer cmd;
    FrameContext* frame;

    // frame-visible targets / handles
    RenderTarget* hdr;
    RenderTarget* ldr;
    RenderTarget* depth;

    // transient frame allocators / profiling hooks
    BufferPool* cpu_pool;
    BufferPool* staging_pool;
} FramePacket;
```

Goal: one source of truth for per-frame orchestration.

### 3. Separate scene ownership from render submission (next)

Split mental and API boundaries:
- Scene layer: entities, transforms, animation, visibility, LOD decisions.
- Render submission layer: consumes visible instances, batches, uploads, emits indirect draws.

Goal: avoid model API collapse into a multipurpose subsystem.

### 4. Stop expanding implicit global access (next)

Keep global renderer for top-level orchestration, but new subsystems should consume explicit context slices.

Patterns to prefer:
- RenderFrameContext
- RenderResourceContext
- RenderDeviceContext

Goal: reduce coupling slope before it hardens.

### 5. Enforce push-constant policy (ongoing)

Rule set:
- Push constants identify data, not store bulk data.
- Keep to IDs/offsets/addresses/scalars.
- Move expandable state to structured buffers.

Goal: retain fast and stable push usage under feature growth.

## What Not to Do Yet

Avoid premature framework construction:
- Full ECS rewrite.
- Full automatic render-graph compiler.
- Descriptor specialization per material family.
- Generic reflection-heavy meta frameworks.

Current bottlenecks are structural clarity and ownership boundaries, not missing meta-abstractions.

## Suggested Implementation Sequence

1. Add lightweight pass contract metadata and log it during frame build.
2. Introduce FramePacket and thread it through pass entry points.
3. Refactor model API boundary into submission-focused interfaces.
4. Freeze push-constant policy and add checks/lints where practical.
5. Migrate new features to explicit context slices, not global renderer reach-through.

## Bottom Line

This is a strong renderer with correct foundational direction.

The next phase is not “more clever Vulkan.”
The next phase is making dataflow and ownership harder to rot.

If that is done now, this architecture will age well under feature pressure instead of becoming implicit-engine folklore.
