# 2D ECS Layer Build Guide

## Why this document exists

This is a separate, implementation-first guide for building a 2D gameplay/render layer with ECS in this codebase.

It complements:
- `docs/2D_GAME_API_DESIGN.md` (renderer-facing 2D API ideas)
- `docs/2D_ECS_LAYER_DESIGN.md` (high-level ECS design)
- `mu/docs/entity.md` + `entity-1..5.md` (entity lifetime, SoA components, hierarchy, resource spawn, prefab merge)

The goal here is to define **what to build first**, **how data flows**, and **how it must integrate with current renderer constraints**.

---

## Hard constraints from this renderer

1. Single descriptor set layout and single pipeline layout (bindless model) are used across pipelines.
2. GPU data is uploaded into large buffers and suballocated; systems pass offsets/ranges.
3. Sprite rendering must not require dedicated per-sprite vertex/index buffers.
4. Draw commands pass buffer slice offsets via push constants.

Implication: the 2D ECS layer should produce compact per-frame instance streams and reference them by offset.

---

## Target architecture

```text
Authoring (SJSON, prefabs)
  -> compile flatten/merge
  -> grouped entity resource blob (by component type)
  -> runtime spawn into ECS world (SoA component managers)
  -> frame systems update gameplay + transforms
  -> 2D extraction builds sorted render stream
  -> renderer uploads stream slice and draws using push-constant offsets
```

Runtime ownership split:
- ECS owns gameplay state (`Transform2D`, `Sprite2D`, `Camera2D`, `Layer2D`).
- Renderer owns GPU objects and submission.
- Extraction layer is the bridge, not a second gameplay model.

---

## Minimal component set (MVP)

## `Transform2D`
Required for renderables.

Fields (conceptual):
- local position, rotation, scale
- world affine (2x3)
- hierarchy links (parent/first_child/next_sibling/prev_sibling)
- owner entity

Storage:
- SoA in one allocation
- dense index handles
- swap-erase removal + link repair

Update policy:
- immediate subtree propagation (default)
- optional batched recompute API later if profiling requires

## `Sprite2D`
Renderable data only:
- bindless `texture_id`
- uv rect
- color/tint
- size and pivot
- coarse order fields (`layer`, `material_key`)
- owner entity

Storage:
- SoA dense arrays
- fast `Entity -> instance` lookup

## `Camera2D`
At least one active camera per pass:
- position, rotation, zoom
- near/far
- active bit

## `Layer2D` (optional, but useful early)
Defines pass and sorting policy:
- pass (`world`, `ui`, `debug`)
- sort mode
- order bias

---

## Runtime systems and order

Recommended frame order:

1. Input/gameplay systems write locals (`Transform2D`) and sprite properties.
2. Transform system updates world transforms.
3. Optional animation/physics post-writeback.
4. 2D extraction system gathers visible sprites and builds sort keys.
5. Extraction emits packed instance stream by draw range.
6. Renderer uploads stream into transient suballocated slice.
7. Renderer records draws using push constants:
   - base/offset to instance data
   - range (`first_instance`, `instance_count`)
   - camera constants offset

Keep extraction deterministic by using a stable tie-break key (`entity_id`) after pass/layer/material/texture.

---

## Render stream contract (CPU -> GPU)

Define one packed instance struct for the 2D shader path.

Suggested content:
- world affine (or position/rotation/scale if shader reconstructs)
- uv rect
- tint
- texture id
- any per-draw flags in a packed field

Design rules:
- 16-byte alignment where practical
- no pointers in stream data
- all GPU addressing via buffer offsets/addresses already supported by backend

Push constants should carry only cheap scalar routing info:
- instance stream slice offset/address
- first/count for this draw range
- camera data offset

No per-sprite vertex buffer uploads are needed; quad corners are derived from `gl_VertexIndex`.

---

## Spawn and resource pipeline

Use the entity-series approach directly:

1. Flatten prefab chain at compile time.
2. Apply additive/modified/deleted components and children by GUID.
3. Validate schema/version per component payload.
4. Emit grouped resource blocks by component type.
5. Runtime spawn in batches:
   - create all entities first
   - spawn transforms for all transform instances
   - spawn sprites for all sprite instances
   - spawn cameras/layers

Why grouped spawn:
- linear memory access
- lower instruction-cache churn
- easy skipping of unknown component blocks

---

## API boundary (clean separation)

ECS-side API should express gameplay intent, not renderer internals.

Examples:
- `entity_create/destroy`
- `add_transform2d`, `set_parent`
- `add_sprite2d`, `set_sprite_texture`, `set_sprite_uv`
- `set_active_camera2d`
- `world2d_tick`

Renderer integration API should stay narrow:
- `extract_2d_draw_stream(world, pass, out_stream)`
- `submit_2d_stream(stream_slice, camera_slice)`

This avoids coupling gameplay code to Vulkan-specific details while still honoring the existing backend design.

---

## Data structure guidance from entity notes

From `entity-1..5.md`, keep these defaults:

- Entity IDs are index+generation handles.
- Component managers are dense SoA with swap-erase.
- Use array map for dense components (`Transform2D`, likely `Sprite2D`).
- Use hash map only for truly sparse components.
- Keep hierarchy links as indices, not pointers.
- Use grouped spawn from binary resource blobs.
- Do prefab flattening at build time, never in hot runtime path.

---

## Implementation plan (recommended sequence)

### Phase 1: ECS core hookup
- Reuse existing entity manager semantics (weak handle + generation).
- Add/verify `Transform2D` manager with hierarchy-safe swap-erase.
- Add `Sprite2D` manager with dense lookup.
- Add one active `Camera2D` path.

### Phase 2: Extraction + draw path
- Implement extraction loop over active camera + visible sprites.
- Build deterministic sort keys.
- Emit packed instance stream.
- Upload stream via offset allocator slices.
- Draw using push-constant offsets and shader-generated quad topology.

### Phase 3: Authoring + spawn
- Add/extend resource compiler path for grouped component blobs.
- Support prefab flattening with add/modify/delete merge semantics.
- Batch spawn entities/components by type.

### Phase 4: Validation and perf gates
- Correctness tests: hierarchy propagation, delete stability, deterministic order.
- Stress tests: create/destroy churn, large sprite counts, heavy layer mixing.
- Perf checks: extraction time, upload bandwidth, draw count after batching.

---

## Risk checklist

1. **Hierarchy corruption on swap-erase**
   - Mitigation: centralize link patch-up helpers and assert invariants in debug.

2. **Nondeterministic draw order**
   - Mitigation: stable sort key with deterministic tie-break (`entity_id`).

3. **Prefab override ambiguity**
   - Mitigation: strict GUID-targeted merge and compile-time diagnostics.

4. **Over-coupling ECS and renderer**
   - Mitigation: extraction output is plain packed data; renderer remains consumer only.

---

## Done criteria for MVP

MVP is complete when:
- You can spawn a prefab-authored 2D scene into ECS.
- Moving parent transforms updates child sprite world transforms correctly.
- Render extraction produces deterministic ordering across runs.
- The renderer consumes one or more stream slices using push-constant offsets.
- No dedicated sprite vertex/index buffers are introduced.
- The path coexists with existing renderer pipeline layout and bindless descriptors.

---

## Suggested next docs to keep in sync

- `docs/2D_ECS_LAYER_DESIGN.md` (high-level architecture)
- `docs/2D_GAME_API_DESIGN.md` (public API evolution)
- `docs/RENDERER_API_REFERENCE.md` (submission contract)
- `mu/docs/entity-4.md` and `mu/docs/entity-5.md` (resource and prefab semantics)
