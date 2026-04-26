# Model API Refactor: Runtime-First Asset Pipeline

Status: Proposed architecture and migration plan.

## Core Decision

glTF is an interchange format. It is import input only.

Runtime truth must be a strict engine-owned format with predictable layout and explicit metadata.

Target pipeline:

```text
        DCC Tool
   (Blender, Maya, etc)
              |
              v
           glTF/GLB
     (exchange/import only)
              |
              v
     offline converter tool
   (validate + normalize + pack)
              |
              v
         .meshx (+ .geom)
      (runtime/editor truth)
              |
              v
      engine model asset API
```

T'he renderer and gameplay runtime must never parse glTF directly.

## Goals

- predictable data layout
- explicit required channels
- no optional runtime chaos
- no pointer forests in hot paths
- debuggable asset visibility in text form

## Non-Goals

- making glTF more convenient at runtime
- preserving all authoring metadata in shipped assets
- introducing per-loader runtime branching for exporter quirks

## Asset Format Plan

### Phase 1: Text First

Use a rigid, tagged text format named .meshx for complete inspectability.

Rules:

- fixed sections and order
- required keys for runtime-required channels
- unknown tags are hard errors in strict mode
- no JSON
- ASCII text output

### Phase 2: Split Manifest and Geometry

- .meshx: manifest and metadata
- .geom: packed binary streams

Runtime loads .meshx metadata first, then uploads .geom bytes.

## .meshx Minimal Schema (Phase 1)

```text
mesh "crate"

bounds
{
    min -0.5 -0.5 -0.5
    max  0.5  0.5  0.5
}

vertex_layout
{
    position f32x3
    normal   f32x3
    uv0      f32x2
    tangent  f32x4
}

material 0
{
    name "crate_wood"
    base_color_tex "crate_albedo.ktx2"
    normal_tex     "crate_normal.ktx2"
    orm_tex        "crate_orm.ktx2"

    base_color_factor 1.0 1.0 1.0 1.0
    metallic  0.1
    roughness 0.8
    alpha_mode opaque
}

submesh 0
{
    material 0
    vertex_offset 0
    vertex_count 24
    index_offset 0
    index_count 36

    bounds
    {
        min -0.5 -0.5 -0.5
        max  0.5  0.5  0.5
    }
}

vertices
{
    v -0.5 -0.5 -0.5   0 0 -1   0 0   1 0 0 1
    v  0.5 -0.5 -0.5   0 0 -1   1 0   1 0 0 1
}

indices
{
    i 0 1 2
    i 2 3 0
}
```

## Converter Responsibilities

Converter is the firewall between import chaos and engine truth.

### Parse

- load glTF/GLB
- decode buffers and accessors

### Validate

- unsupported topology rejected
- bad or out-of-range indices rejected
- required channels enforced
- missing UVs or tangents flagged per policy
- material reference validity checked

### Normalize

- triangulate primitives if needed
- flatten node hierarchy
- apply node transforms to final geometry
- resolve and remap materials
- merge/partition primitives by runtime policy

### Enrich

- generate normals when absent
- generate tangents when absent and UVs exist
- compute per-submesh and model bounds
- quantize/compress streams (policy controlled)

### Emit

- emit strict .meshx text in Phase 1
- emit .meshx + .geom in Phase 2

## Runtime Model API Direction

Current public surface in gltf_gpu_mesh.h is glTF-named and should move to format-neutral model API naming.

Proposed API (phase-friendly):

```c
typedef uint32_t ModelHandle;

bool model_api_init(uint32_t max_models, uint32_t instance_capacity);
void model_api_shutdown(void);

bool model_api_load_meshx(const char* path, ModelHandle* out_model);
bool model_api_find_or_load_meshx(const char* path, ModelHandle* out_model);
void model_api_unload(ModelHandle model);

void model_api_begin_frame(const Camera* cam);
bool model_api_draw(ModelHandle model, const float model_matrix[4][4], const float color[4]);
void model_api_prepare_frame(VkCommandBuffer cmd);
void model_api_draw_queued(VkCommandBuffer cmd);
void model_api_flush_frame(VkCommandBuffer cmd);
```

Backward-compatible wrappers may remain temporarily:

- model_api_load_gltf calls converter output lookup or compatibility import path
- draw3d forwards to model_api_draw

## Data Model Requirements

- handle-based model lookup via mu_id_pool
- dense submesh/material tables
- per-model range: first_submesh + submesh_count
- fixed-size GPU metadata records, 16-byte aligned
- cold strings and debug metadata separated from hot render loop arrays

## Migration Plan

### Step 1

- keep existing renderer queue and submesh path
- introduce .meshx parser and loader entry points
- add temporary compatibility wrappers so call sites keep working

### Step 2

- add offline converter executable
- convert sample assets to .meshx
- switch main runtime loads to model_api_load_meshx

### Step 3

- remove runtime glTF decode dependency from shipping path
- keep glTF import in toolchain only

### Step 4

- introduce optional .geom binary sidecar
- keep .meshx schema stable as manifest

## Validation Checklist

Converter output must guarantee:

- indices are valid for emitted vertex range
- declared vertex_layout matches stream payload
- all submeshes reference valid materials
- model and submesh bounds are present and finite
- texture references are normalized and resolvable

Runtime load must fail fast on violations.

## Implementation Notes For This Repository

- keep single descriptor set and bindless flow unchanged
- keep push constant payload 16-byte aligned
- preserve offset-based buffer slice model
- add parser/token helpers in helpers.h/helpers.c where shared utility is needed

## What Changes In Practice

- glTF becomes import-only tool input
- .meshx becomes runtime/editor source of truth
- model API names become format-neutral
- asset stability improves because runtime consumes controlled schema, not DCC exporter variance
