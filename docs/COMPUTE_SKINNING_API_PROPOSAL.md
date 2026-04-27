# Compute Skinning and Model Animation Mode API Proposal

Status: Proposed  
Date: 2026-04-27  
Scope: Runtime API shape before compute skinning implementation

## Goals

1. Add compute skinning without breaking current model draw flow.
2. Keep API simple for game code.
3. Preserve current renderer architecture: bindless descriptors, suballocated buffers, push-constant offsets.
4. Make animation mode explicit per model instance.

## Non-Goals (for first implementation)

1. Full animation graph/state machine API.
2. Blend trees and additive layers.
3. Morph target runtime playback.
4. Network synchronization features.

## Current Baseline

Current model API supports:

1. MeshX loading.
2. Queued model draw submission.
3. Indirect draw generation.

Current public entry points are in [gltf_gpu_mesh.h](gltf_gpu_mesh.h).

There is currently no public animation control API and no explicit skinning mode selector.

## Proposed API

### 1. Animation mode enum

```c
typedef enum ModelAnimationMode
{
    MODEL_ANIMATION_MODE_STATIC = 0,
    MODEL_ANIMATION_MODE_VERTEX_SKINNING = 1,
    MODEL_ANIMATION_MODE_COMPUTE_SKINNING = 2,
    MODEL_ANIMATION_MODE_AUTO = 3,
} ModelAnimationMode;
```

Behavior:

1. `MODEL_ANIMATION_MODE_STATIC`:
   1. Ignores animation tracks.
   2. Uses bind pose or authored static geometry.
2. `MODEL_ANIMATION_MODE_VERTEX_SKINNING`:
   1. Uses graphics pipeline skinning path.
   2. Useful as fallback and debug parity path.
3. `MODEL_ANIMATION_MODE_COMPUTE_SKINNING`:
   1. Uses compute pre-skin pass.
   2. Graphics reads skinned output buffer.
4. `MODEL_ANIMATION_MODE_AUTO`:
   1. Runtime chooses compute for eligible skinned models.
   2. Falls back to vertex skinning or static when needed.

### 2. Per-instance animation state handle

```c
typedef uint32_t ModelAnimHandle;
#define MODEL_ANIM_HANDLE_INVALID UINT32_MAX
```

Rationale:

1. Keep `ModelHandle` for immutable asset identity.
2. Store playback mode/time per spawned instance using `ModelAnimHandle`.

### 3. Core control APIs

```c
bool model_anim_create(ModelHandle model, ModelAnimHandle* out_anim);
void model_anim_destroy(ModelAnimHandle anim);

bool model_anim_set_mode(ModelAnimHandle anim, ModelAnimationMode mode);
ModelAnimationMode model_anim_get_mode(ModelAnimHandle anim);

bool model_anim_play_clip(ModelAnimHandle anim, uint32_t clip_index, bool loop);
bool model_anim_set_time(ModelAnimHandle anim, float time_seconds);
bool model_anim_set_rate(ModelAnimHandle anim, float playback_rate);

bool model_anim_pause(ModelAnimHandle anim, bool paused);
bool model_anim_is_paused(ModelAnimHandle anim, bool* out_paused);

bool model_anim_set_enabled(ModelAnimHandle anim, bool enabled);
```

Minimal draw API addition:

```c
bool model_api_draw_animated(ModelAnimHandle anim,
                             const float model_matrix[4][4],
                             const float color[4]);
```

Design choice:

1. Keep existing `model_api_draw` untouched for static/simple usage.
2. Add `model_api_draw_animated` for animation-aware submission.

### 4. Query APIs for tooling/debug

```c
bool model_anim_clip_count(ModelHandle model, uint32_t* out_count);
bool model_anim_clip_name(ModelHandle model, uint32_t clip_index, const char** out_name);
```

These are useful for UI and debug tooling without exposing internal storage.

## Internal Data Model (first pass)

### 1. Model asset additions

Each loaded model asset should cache:

1. `has_skin` flag.
2. `skeleton_joint_count`.
3. `clip_count`.
4. Clip metadata (`name`, `duration_seconds`).

### 2. Animation instance record

Each `ModelAnimHandle` stores:

1. `model` handle.
2. `mode`.
3. `enabled`.
4. `paused`.
5. `clip_index`.
6. `loop`.
7. `time_seconds`.
8. `playback_rate`.
9. `dirty_pose` and `dirty_skin` bits.

### 3. Mode resolution in AUTO

Resolution order:

1. If model has no skin: `STATIC`.
2. If compute skinning unsupported this frame: `VERTEX_SKINNING`.
3. Else: `COMPUTE_SKINNING`.

## Frame Pipeline Integration

The frame order should be:

1. `model_api_begin_frame`.
2. Animation update for active `ModelAnimHandle` records.
3. Build joint palettes.
4. Compute skinning dispatch for records in compute mode.
5. Buffer barrier: compute write -> graphics read.
6. `model_api_prepare_frame` and draw.

This aligns with [docs/COMPUTE_SKINNING_GUIDE.md](docs/COMPUTE_SKINNING_GUIDE.md).

## Error Handling Contract

All functions return `false` on invalid handle, invalid clip index, or uninitialized runtime state.

Rules:

1. Invalid mode for non-skinned model should not crash.
2. For non-skinned models, mode requests resolve to `STATIC`.
3. `model_anim_play_clip` fails if clip index is out of range.

## Threading Assumptions (first pass)

1. API calls happen on main thread.
2. Internal compute dispatch and uploads happen during render command recording.
3. No concurrent mutation of animation state while frame submission is in progress.

## Suggested Header Placement

Keep first version in existing model API surface:

1. Add enum and animation API declarations to [gltf_gpu_mesh.h](gltf_gpu_mesh.h).
2. Implement in [gltf_gpu_mesh.c](gltf_gpu_mesh.c).

If the surface grows later, split to a dedicated `model_animation.h`.

## Migration Plan

### Phase 1: API scaffold only

1. Add types and function declarations.
2. Stub implementation with strict validation.
3. Support `STATIC` and `AUTO` fallback behavior.

### Phase 2: Vertex skinning mode hookup

1. Parse and cache clip metadata.
2. Advance animation time.
3. Use vertex skinning path for animated models.

### Phase 3: Compute skinning mode hookup

1. Add palette and skinned output buffer slices.
2. Dispatch compute skinning before graphics draw.
3. Add barriers and debug counters.

## Example Usage

```c
ModelHandle model = MODEL_HANDLE_INVALID;
ModelAnimHandle anim = MODEL_ANIM_HANDLE_INVALID;

model_api_find_or_load_meshx("gameassets/blockychar/character-a.meshx", &model);
model_anim_create(model, &anim);

model_anim_set_mode(anim, MODEL_ANIMATION_MODE_COMPUTE_SKINNING);
model_anim_play_clip(anim, 1u, true);
model_anim_set_rate(anim, 1.0f);

model_api_draw_animated(anim, world, color);
```

## Open Questions

1. Should `model_api_draw` implicitly use animation state if one exists, or always require `model_api_draw_animated`?
2. Should clip selection be by index only, or index + name lookup helper?
3. Should compute skinning mode be globally toggleable for quick fallback testing?

## Decision Recommendation

For first implementation:

1. Add explicit `ModelAnimHandle`.
2. Add explicit `model_api_draw_animated`.
3. Keep `model_api_draw` behavior unchanged.
4. Implement `AUTO` mode fallback logic from day one.

This keeps risk low while giving a clean migration path to full compute skinning.
