# 2D Layer with ECS — Data-Oriented Design

## Purpose

This document defines a **separate** 2D layer architecture built on the entity/component model, while reusing the existing renderer backend.

Scope:
- ECS-first gameplay and scene authoring for 2D.
- Deterministic transform/render ordering.
- Rendering integration that respects current renderer constraints:
  - single descriptor set layout + single pipeline layout (bindless model)
  - GPU buffers are suballocated with offsets
  - no dedicated vertex/index buffers for sprite data; draw via offsets passed in push constants

Non-goals:
- Replacing the existing 3D/clustered renderer.
- Introducing a second resource lifetime model.
- Runtime prefab graph evaluation (prefabs are flattened at build/compile time).

---

## Design Principles

1. **Entity is a value, not an object**
   - Entity handles are weak and generation-validated.
   - Systems store entity IDs and component instances, never cross-owning pointers.

2. **Components are dense SoA**
   - Component managers own contiguous arrays and `Entity -> instance` mapping.
   - Default removal is swap-erase.

3. **Batch work by component type**
   - Spawn and update paths process homogeneous arrays.
   - Resource compilation groups data by component type.

4. **2D render extraction is explicit**
   - Gameplay data writes `Sprite2D`/`Transform2D`.
   - Render system builds transient draw streams and GPU slices each frame.

---

## High-Level Architecture

```text
Gameplay / Tools
    |
    v
ECS World (EntityManager + ComponentManagers)
    |- Transform2D
    |- Sprite2D
    |- Camera2D
    |- Layer2D
    |- Optional: Animator2D, PhysicsBody2D, Text2D
    |
    v
2D Render Extraction System
    |- visibility + layer sort + material grouping
    |- writes transient instance stream
    |
    v
Renderer Backend (existing Vulkan model)
    |- bindless textures/samplers
    |- offset allocator suballocations
    |- push constants carry buffer offsets/ranges
```

---

## Core ECS Components

## 1) `Transform2D`

Required for renderable entities.

```c
typedef struct {
    Entity   entity;
    uint32_t parent_instance;  // INVALID_U32 if root

    float    local_pos_x;
    float    local_pos_y;
    float    local_rot;
    float    local_scale_x;
    float    local_scale_y;

    // Cached world transform (2x3 affine is enough for 2D)
    float    world_m00, world_m01, world_m02;
    float    world_m10, world_m11, world_m12;
} Transform2D;
```

Notes:
- Immediate subtree propagation is valid default for shallow gameplay trees.
- Optional batched recompute mode can be added if profiling shows long-chain edits.

## 2) `Sprite2D`

```c
typedef struct {
    Entity   entity;
    uint32_t texture_id;    // bindless ID

    float    uv_min_x;
    float    uv_min_y;
    float    uv_max_x;
    float    uv_max_y;

    float    color_r;
    float    color_g;
    float    color_b;
    float    color_a;

    float    pivot_x;       // normalized [0..1]
    float    pivot_y;       // normalized [0..1]

    float    size_x;
    float    size_y;

    uint16_t layer;         // coarse order bucket
    uint16_t material_key;  // blend/filter/shader variant bucket
} Sprite2D;
```

## 3) `Camera2D`

```c
typedef struct {
    Entity   entity;
    float    x;
    float    y;
    float    zoom;
    float    rotation;
    float    near_z;
    float    far_z;
    uint8_t  active;
} Camera2D;
```

## 4) `Layer2D`

Optional policy component for per-entity pass routing.

```c
typedef struct {
    Entity   entity;
    uint8_t  pass;          // world/ui/debug
    uint8_t  sort_mode;     // stable_y, explicit_z, none
    uint16_t order_bias;
} Layer2D;
```

---

## Systems and Frame Order

1. **Input/Gameplay Systems**
   - Update gameplay components.

2. **Transform2D System**
   - Apply local changes and update cached world transforms.

3. **Animation/Physics (optional)**
   - Write back to transforms/sprites.

4. **2D Render Extraction System**
   - Iterate visible cameras.
   - Collect visible sprites.
   - Build sort keys:
     - pass
     - layer
     - material/texture grouping
     - depth tie-break
   - Emit packed per-draw instance stream.

5. **Renderer Submission**
   - Allocate transient GPU slice via offset allocator.
   - Upload stream.
   - Record draw calls with push constants containing offsets/counts.

---

## Render Data Path (No Vertex/Index Buffers)

A draw uses:
- one static quad topology generated in shader from `gl_VertexIndex`
- one instance stream read from a buffer address/offset
- bindless texture lookup by `texture_id`

Push constants for each draw range:

```c
typedef struct {
    uint64_t instance_buffer_gpu_address; // or backend-defined offset handle
    uint32_t first_instance;
    uint32_t instance_count;
    uint32_t camera_data_offset;
    uint32_t reserved;
} Push2D;
```

The vertex shader derives quad corners from `gl_VertexIndex` and fetches instance data by `first_instance + gl_InstanceIndex`.

This matches current engine constraints:
- no per-sprite vertex/index buffers
- offsets/ranges drive buffer access
- one global pipeline layout and descriptor model

---

## Suggested ECS Storage

For hot components (`Transform2D`, `Sprite2D`):
- SoA arrays in one allocation per component manager.
- `Entity.index -> instance` array mapping where component density is high.

For sparse components (`Layer2D`, optional gameplay tags):
- hash map mapping can be acceptable.

Removal policy:
- swap-erase by default.
- if strict order is required, keep a sort key array instead of stable component indices.

---

## Authoring, Prefabs, and Spawn

Use existing prefab merge model:
- additive: `components`
- override: `modified_components`
- delete: `deleted_components`

Build pipeline:
1. Flatten prefab chains at compile time.
2. Validate component schema/version.
3. Group instances by component type in resource blobs.
4. Spawn with batch loops (`create entities`, then `spawn Transform2D`, `spawn Sprite2D`, etc).

This keeps runtime spawn linear and cache-friendly.

---

## Minimal C-Style API Surface

```c
typedef uint32_t Entity;
typedef struct World2D World2D;

typedef struct {
    float x, y;
    float rot;
    float sx, sy;
} Transform2DInit;

typedef struct {
    uint32_t texture_id;
    float u0, v0, u1, v1;
    float r, g, b, a;
    float px, py;
    float w, h;
    uint16_t layer;
    uint16_t material_key;
} Sprite2DInit;

World2D* world2d_create(void);
void     world2d_destroy(World2D* w);

Entity   world2d_entity_create(World2D* w);
void     world2d_entity_destroy(World2D* w, Entity e);

void     world2d_add_transform(World2D* w, Entity e, const Transform2DInit* t);
void     world2d_add_sprite(World2D* w, Entity e, const Sprite2DInit* s);
void     world2d_set_parent(World2D* w, Entity child, Entity parent);

void     world2d_tick(World2D* w, float dt);
void     world2d_render(World2D* w, uint32_t active_camera_entity);
```

---

## Migration Plan from Current 2D API Doc

1. Keep current renderer-facing concepts (bindless texture IDs, offset allocation, push constants).
2. Replace scene-graph-centric ownership with ECS ownership.
3. Move batching from "SpriteBatch object owns everything" to "RenderExtraction system consumes ECS state".
4. Keep shader-side quad generation and bindless sampling.

---

## Validation Checklist

- Entity lifetime safety: stale handles never resolve after generation bump.
- Transform correctness: parent-child updates match expected world transforms.
- Swap-erase integrity: maps and hierarchy links remain valid after deletions.
- Render determinism: same ECS state yields stable draw order.
- GPU path correctness: push-constant offsets point at valid suballocated slices.
- Throughput targets: stress test with large sprite counts and frequent create/destroy.

---

## Practical Defaults

- Start with immediate transform propagation.
- Single active `Camera2D` per pass.
- Sort key = `(pass, layer, material_key, texture_id, entity_id)`.
- One transient instance stream allocation per pass per frame.
- Double/triple buffered transient slices using frame index.

These defaults are simple, performant, and align with the existing renderer architecture.