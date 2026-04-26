# Model API Refactor Proposal

Status: Data-oriented refactor plan

## Current State vs Future State

### What's Currently Good

- ✅ Bindless-friendly design
- ✅ Indirect command driven
- ✅ GPU pointer fetch model
- ✅ Model/material split
- ✅ Lazy upload strategy
- ✅ Proper staging barriers
- ✅ Sorted instance batching
- ✅ Simple API surface
- ✅ No descriptor rebinding

### The 5 Architectural Landmines

| Problem | Current | Future | Pain Level |
|---------|---------|--------|-----------|
| Asset ↔ Renderer fusion | Single blob | Split systems | 🔥🔥🔥 |
| Multi-mesh support | First mesh only | Full glTF hierarchy | 🔥🔥🔥 |
| Sort key completeness | Model only | Pipeline→Material→Mesh | 🔥🔥 |
| Indirect building | CPU per-frame | GPU compute later | 🔥 |
| Draw submission | Path-based | Handle-based | 🔥🔥🔥 |

---

## Data-Oriented Direction

The model API should move from a single mixed-lifetime blob toward `mu_`-allocated table IDs, dense child tables, and frame-local streams.

The current renderer already wants this shape:

- Shaders fetch model, material, and instance data through GPU pointers.
- Buffers are suballocated from large pools.
- Draws are submitted with bindless texture/sampler IDs and push constants.
- Indirect rendering works best when adjacent commands read adjacent metadata.

The refactor should make the CPU side match the GPU side:

```text
Long-lived asset data
    ID-indexed model table
    dense submesh table
    dense material table
    packed GPU mesh streams
    packed GPU metadata tables

Per-frame render data
    append-only draw request arrays
    compact sort key/index array
    packed instance stream
    packed indirect command stream
```

Do not model this as "model objects with pointers to child objects" in hot paths. Store child data in central tables, and let models reference ranges.

```text
ModelHandle -> model table slot
Model table slot -> first_submesh + submesh_count
Submesh table slot -> mesh stream offsets + material index + bounds
Material table slot -> bindless texture IDs + compact constants
Draw request -> model index + transform/color payload index
```

### Data Layout Rules

1. Model handles are table IDs allocated with `mu_id_pool`, matching how `TextureID` and `SamplerID` are generated.
2. Model tables are ID-indexed slot arrays. Hot iteration can use a separate dense `active_model_ids` array if needed.
3. A model owns a range of submesh IDs, not a heap allocation of `Submesh*`.
4. A frame queue owns append-only arrays and resets every frame.
5. Sorting should move compact keys/indices first. Large instance payloads should be written once in final sorted order.
6. Path strings are asset-system input only. No strings in per-frame draw submission.
7. Asset loading may use temporary pointer-rich decode structures, but runtime tables should be compact.
8. GPU-visible records should be fixed-size and 16-byte aligned.

### Hot vs Cold Data

Keep hot render-loop data small:

```text
Hot
    model handle
    submesh index
    material index
    sort key
    transform/color instance payload
    bounds for culling
    indirect command data

Cold
    source path
    debug name
    decoded glTF node names
    CPU mesh decode scratch
    material authoring metadata
```

Cold data can live in separate arrays or editor-only structures. The render queue should not touch it.

---

## Architecture: Phase 1 (Now) → Phase 2 (GPU Indirect)

```
CURRENT STATE (Single Blob)
━━━━━━━━━━━━━━━━━━━━━━━━━
        g_model_api
       /    |    \
   asset  frame  draw
  lifetime render submit
  
PHASE 1 REFACTOR (Split Systems)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  ModelAssetSystem       ModelRenderQueue
  (long-lived)          (per-frame)
  ├─ model ID slots     ├─ append draw requests
  ├─ dense submeshes    ├─ build sort keys
  ├─ dense materials    ├─ sort compact records
  ├─ packed buffers     ├─ write instance stream
  ├─ residency          └─ emit indirect stream
  └─ handles/ranges

PHASE 2 FUTURE (GPU Compute)
━━━━━━━━━━━━━━━━━━━━━━━━━━
  ModelAssetSystem       ModelRenderQueue       GPU Culling Compute
  (unchanged)           (thin submit layer)    (do the real work)
```

---

## Phase 1: Split Systems (Do This First)

### System 1: ModelAssetSystem (Long-Lived)

**Responsibilities:**
- Load models from disk
- Own mesh/material/texture data
- Manage GPU residency
- Generate and hand out `ModelHandle`s
- Nothing per-frame

```c
/* ========== ASSET SYSTEM ==========*/

typedef uint32_t ModelHandle;
typedef uint32_t SubmeshId;
typedef uint32_t MaterialId;

#define MODEL_HANDLE_INVALID UINT32_MAX

typedef struct {
    uint32_t vertex_offset;     // In global vertex stream
    uint32_t index_offset;      // In global index stream
    uint32_t index_count;
    uint32_t vertex_count;
    MaterialId material_id;     // Into dense material table
    uint32_t flags;
    AABB bounds;                // For culling
} Submesh;

typedef struct {
    uint32_t first_submesh_id;  // Global submesh table index
    uint32_t submesh_count;
    AABB bounds;
} ModelData;

typedef struct {
    uint32_t base_color_texture_id;
    uint32_t normal_texture_id;
    uint32_t orm_texture_id;
    uint32_t sampler_id;
    float base_color_factor[4];
    float metallic_factor;
    float roughness_factor;
    uint32_t flags;
} MaterialData;

typedef struct {
    BufferSlice dst;
    const void *src;
    uint32_t size;
    uint32_t alignment;
} UploadRequest;

typedef struct {
    /* Handle allocation mirrors TextureID/SamplerID allocation. */
    mu_id_pool model_id_pool;

    /* ID-indexed long-lived model slots */
    ModelData *models;
    uint32_t model_count;      /* live model count */
    uint32_t model_capacity;
    uint32_t *active_model_ids; /* optional dense list for iteration/debug */
    uint32_t active_model_count;
    
    /* Global submesh table */
    Submesh *submesh_table;
    uint32_t submesh_count;
    uint32_t submesh_capacity;

    /* Global material table */
    MaterialData *material_table;
    uint32_t material_count;
    uint32_t material_capacity;

    /* Cold metadata. Do not touch this during frame rendering. */
    char **debug_paths;
    
    /* GPU buffers / slices (never resized mid-frame) */
    BufferSlice position_stream;
    BufferSlice normal_stream;
    BufferSlice uv_stream;
    BufferSlice index_stream;
    BufferSlice model_meta_table;
    BufferSlice material_gpu_table;
    
    /* Offset allocators for incremental model loads */
    OffsetAllocator vertex_alloc;
    OffsetAllocator index_alloc;
    
    /* Pending uploads for next frame, batched as records */
    UploadRequest *pending_uploads;
    uint32_t pending_upload_count;
    uint32_t pending_upload_capacity;
    
} ModelAssetSystem;

static inline bool model_handle_valid(const ModelAssetSystem *sys, ModelHandle h)
{
    return h != MODEL_HANDLE_INVALID &&
           h < sys->model_capacity &&
           mu_id_pool_is_id(&sys->model_id_pool, h);
}

/* Load a full glTF (all meshes + primitives) */
ModelHandle model_asset_load_gltf(
    ModelAssetSystem *sys,
    const char *path
);

/* Get model data (read-only) */
const ModelData *model_asset_get(
    ModelAssetSystem *sys,
    ModelHandle h
);

/* Unload a model and free its GPU residency */
void model_asset_unload(
    ModelAssetSystem *sys,
    ModelHandle h
);

/* GPU buffer setup (called once at init) */
void model_asset_init_gpu_buffers(
    ModelAssetSystem *sys,
    VkDevice device,
    VmaAllocator allocator,
    uint32_t vertex_buffer_size,
    uint32_t index_buffer_size
);

/* Upload pending models (call once per frame before rendering) */
void model_asset_upload_pending(
    ModelAssetSystem *sys,
    VkCommandBuffer cmd
);
```

**Data-oriented notes:**
- `ModelHandle` is generated the same way `TextureID` is generated: allocate an ID from a `mu_id_pool`, use it as the table slot, and return it to the pool on destroy.
- `ModelData` stores a submesh range, not a pointer to allocated submeshes.
- `Submesh` is compact metadata for culling, sorting, batching, and shader table generation.
- Cold data such as source paths and debug names is kept outside hot tables.
- Pending uploads are batched as upload records, not stored as many tiny per-model blobs.

### Model Handle Allocation

Use the same pattern as `create_texture()`:

```c
bool model_asset_system_init(ModelAssetSystem *sys, uint32_t max_models)
{
    memset(sys, 0, sizeof(*sys));
    mu_id_pool_init(&sys->model_id_pool, max_models);

    sys->model_capacity = max_models;
    sys->models = calloc(max_models, sizeof(ModelData));
    sys->debug_paths = calloc(max_models, sizeof(char *));
    sys->active_model_ids = calloc(max_models, sizeof(uint32_t));
    return sys->models && sys->debug_paths && sys->active_model_ids;
}

ModelHandle model_asset_alloc_handle(ModelAssetSystem *sys)
{
    uint32_t id;
    if (!mu_id_pool_create_id(&sys->model_id_pool, &id))
        return MODEL_HANDLE_INVALID;

    sys->active_model_ids[sys->active_model_count++] = id;
    sys->model_count++;
    return id;
}

void model_asset_free_handle(ModelAssetSystem *sys, ModelHandle h)
{
    if (!model_handle_valid(sys, h))
        return;

    memset(&sys->models[h], 0, sizeof(sys->models[h]));
    mu_id_pool_destroy_id(&sys->model_id_pool, h);
    sys->model_count--;

    /* Remove h from active_model_ids with swap-erase. */
}
```

Important distinction:
- Texture handles are shader-visible bindless descriptor slots.
- Model handles are CPU-side table slots.

Both should be allocated through `mu_id_pool`, but only texture IDs are written directly into material/instance GPU data for shader sampling. Model IDs may be uploaded into instance data only when the model metadata table is indexed by the same ID.

### System 2: ModelRenderQueue (Per-Frame)

**Responsibilities:**
- Accept draw requests
- Sort by optimal key
- Batch by submesh/material
- Build indirect commands
- Draw

```c
/* ========== RENDER QUEUE SYSTEM ==========*/

typedef struct {
    ModelHandle model;
    uint32_t transform_index;
    uint32_t color_index;
    uint32_t flags;
} DrawRequest;

typedef struct {
    uint64_t sort_key;
    uint32_t request_index;
    SubmeshId submesh_id;
} DrawSortItem;

typedef struct {
    uint32_t model_id;
    uint32_t submesh_id;
    uint32_t material_id;
    uint32_t _pad0;
    Mat4 transform;
    Vec4 color;
} ModelInstanceGpu;

typedef struct {
    /* Append-only per-frame inputs */
    DrawRequest *requests;
    uint32_t request_count;
    uint32_t request_capacity;

    Mat4 *transforms;
    Vec4 *colors;
    uint32_t transform_count;
    uint32_t color_count;

    /* Small records used for sorting and batching */
    DrawSortItem *sort_items;
    uint32_t sort_item_count;

    /* Final GPU-facing streams */
    ModelInstanceGpu *instance_data;
    uint32_t instance_count;
    
    VkDrawIndirectCommand *indirect_commands;
    uint32_t indirect_count;
    
    BufferSlice instance_slice;
    BufferSlice indirect_slice;
    
} ModelRenderQueue;

/* Submit a draw request (called from game code per frame) */
void model_queue_draw(
    ModelRenderQueue *queue,
    ModelHandle model,
    Mat4 transform,
    Vec4 color
);

/* Optional narrow API for tools/debug draws that intentionally render one primitive. */
void model_queue_draw_submesh(
    ModelRenderQueue *queue,
    ModelHandle model,
    SubmeshId submesh,
    Mat4 transform,
    Vec4 color
);

/* Sort and batch (called mid-frame) */
void model_queue_prepare_frame(
    ModelRenderQueue *queue,
    const ModelAssetSystem *assets
);

/* Execute all queued draws */
void model_queue_draw_all(
    ModelRenderQueue *queue,
    VkCommandBuffer cmd,
    const GlobalData *global_data
);

/* Clear queue for next frame */
void model_queue_reset(ModelRenderQueue *queue);
```

**Data-oriented notes:**
- `model_queue_draw()` appends compact request data and stores large transform/color payload in side arrays.
- `sort_items` is the array that gets sorted. It is smaller than the full instance payload.
- `instance_data` is written once, in sorted order, immediately before upload.
- `indirect_commands` are emitted in the same order as `instance_data`.
- The whole queue resets at frame end; it does not own model lifetime.

---

## Problem 1: Asset ↔ Renderer Fusion

### Before (Fused)
```c
typedef struct {
    /* Asset lifetime */
    GltfCpuMesh cpu;
    Texture *textures;
    
    /* Per-frame state */
    DrawRequest draw_requests[MAX_INSTANCES];
    uint32_t request_count;
    
    /* Indirect building */
    VkDrawIndirectCommand *indirect_cmds;
} ModelApiBlob;
```

### After (Split + Dense Tables)
```c
/* Asset System (once at load) */
ModelHandle h = model_asset_load_gltf(asset_sys, "model.glb");

/* Per-frame rendering (in frame loop) */
for (int i = 0; i < instance_count; i++) {
    model_queue_draw(
        render_queue,
        h,
        transforms[i],
        colors[i]
    );
}
```

**Key benefit:** Asset system is decoupled from frame rate. Can load/unload models asynchronously in background thread without touching frame renderer.

**Data-oriented benefit:** The render queue only sees integer handles, table ranges, and compact arrays. It does not chase asset-owned pointers while sorting or building indirect commands.

---

## Problem 2: Multi-Mesh glTF Support

### Before (First Mesh Only)
```c
gltf_minimal_load_first_mesh(path, &entry->cpu);
/* Loads primitives[0] of meshes[0] only */
```

### After (Full Hierarchy)
```c
/*
glTF file structure
───────────────────
Model
 ├─ Mesh 0
 │  ├─ Primitive 0 (geometry + material)
 │  ├─ Primitive 1
 │  └─ Primitive N
 ├─ Mesh 1
 └─ Mesh N

Our representation
──────────────────
ModelData has submesh_count = N
Each ModelData stores first_submesh_id + submesh_count
Each Submesh has material_id, stream offsets, counts, and bounds
*/
:
typedef struct {
    uint32_t vertex_offset;     /* In global vertex buffer */
    uint32_t index_offset;      /* In global index buffer */
    uint32_t index_count;
    uint32_t material_id;       /* Into global material table */
    uint32_t primitive_flags;   /* transparency, alpha mask, etc */
    AABB bounds;
} Submesh;

typedef struct {
    uint32_t first_submesh_id;  /* Global submesh index */
    uint32_t submesh_count;
    AABB bounds;
} ModelData;
```

**Loader changes:**

```c
ModelHandle model_asset_load_gltf(ModelAssetSystem *sys, const char *path) {
    // OLD: Load only first mesh/primitive
    // gltf_minimal_load_first_mesh(path, &entry->cpu);
    
    // NEW: Load all meshes and all primitives
    CgltfData *gltf = cgltf_load_file(path);
    
    uint32_t total_submeshes = count_all_primitives(gltf);
    uint32_t first_submesh_id = sys->submesh_count;
    
    for (uint32_t mesh_idx = 0; mesh_idx < gltf->meshes_count; mesh_idx++) {
        cgltf_mesh *mesh = &gltf->meshes[mesh_idx];
        
        for (uint32_t prim_idx = 0; prim_idx < mesh->primitives_count; prim_idx++) {
            cgltf_primitive *prim = &mesh->primitives[prim_idx];
            
            /* Upload this primitive's geometry */
            SubmeshAllocation alloc = allocate_submesh_gpu(sys, prim);
            
            /* Record submesh metadata */
            sys->submesh_table[sys->submesh_count++] = (Submesh){
                .vertex_offset = alloc.vertex_offset,
                .index_offset = alloc.index_offset,
                .index_count = prim->indices->count,
                .material_id = prim->material - gltf->materials,
                .primitive_flags = extract_primitive_flags(prim),
                .bounds = compute_bounds(prim->positions),
            };
        }
    }
    
    /* Store model metadata in a mu_id_pool-allocated slot. */
    ModelHandle h = model_asset_alloc_handle(sys);
    if (h == MODEL_HANDLE_INVALID)
        return MODEL_HANDLE_INVALID;

    sys->models[h] = (ModelData){
        .submesh_count = total_submeshes,
        .first_submesh_id = first_submesh_id,
        .bounds = compute_model_bounds(gltf),
    };
    
    return h;
}
```

---

## Problem 3: Incomplete Sort Key

### Before (Model Only)
```c
static int draw_call_cmp_model(const void *a, const void *b) {
    DrawRequest *da = (DrawRequest*)a;
    DrawRequest *db = (DrawRequest*)b;
    return (int)da->model - (int)db->model;  /* Only sorts by model ID */
}
```

**Problem:** Doesn't account for:
- Material changes within a model
- Transparent vs opaque
- Pipeline state changes

### After (Complete Sort Key + Compact Sort Items)
```c
typedef struct {
    uint64_t pass_id     : 4;      /* opaque, alpha-mask, transparent, depth-only */
    uint64_t pipeline_id : 8;
    uint64_t material_id : 20;
    uint64_t submesh_id  : 24;
    uint64_t depth_key   : 8;      /* optional coarse front/back ordering */
} SortKey;

static inline SortKey build_sort_key(
    const DrawRequest *req,
    SubmeshId submesh_id,
    const ModelAssetSystem *assets
) {
    const Submesh *submesh = &assets->submesh_table[submesh_id];
    const MaterialData *mat = &assets->material_table[submesh->material_id];
    
    uint32_t pipeline_id = 0;
    if (mat->flags & MATERIAL_TRANSPARENT)
        pipeline_id = 1;  /* Transparent pipeline */
    else if (mat->flags & MATERIAL_ALPHA_MASK)
        pipeline_id = 2;  /* Alpha-mask pipeline */
    /* else 0 = opaque */
    
    return (SortKey){
        .pipeline_id = pipeline_id,
        .material_id = submesh->material_id,
        .submesh_id = submesh_id,
    };
}

static int draw_call_cmp_sort_key(const void *a, const void *b) {
    const DrawSortItem *da = (const DrawSortItem*)a;
    const DrawSortItem *db = (const DrawSortItem*)b;
    return (da->sort_key > db->sort_key) - (da->sort_key < db->sort_key);
}

void model_queue_prepare_frame(
    ModelRenderQueue *queue,
    const ModelAssetSystem *assets
) {
    /* Expand model requests to compact per-submesh sort records. */
    queue->sort_item_count = 0;
    for (uint32_t i = 0; i < queue->request_count; i++) {
        DrawRequest *req = &queue->requests[i];
        const ModelData *model = model_asset_get(assets, req->model);

        for (uint32_t j = 0; j < model->submesh_count; ++j) {
            SubmeshId submesh_id = model->first_submesh_id + j;
            SortKey key = build_sort_key(req, submesh_id, assets);
            queue->sort_items[queue->sort_item_count++] = (DrawSortItem){
                .sort_key = pack_sort_key(key),
                .request_index = i,
                .submesh_id = submesh_id,
            };
        }
    }
    
    /* Sort small records, not full transform/color payloads. */
    qsort(queue->sort_items, queue->sort_item_count,
          sizeof(DrawSortItem), draw_call_cmp_sort_key);
    
    /* Write final instance stream in sorted order, then build indirect batches. */
    uint32_t indirect_idx = 0;
    uint32_t current_pipeline = UINT32_MAX;
    uint32_t current_material = UINT32_MAX;
    uint32_t current_submesh = UINT32_MAX;
    
    for (uint32_t i = 0; i < queue->sort_item_count; i++) {
        DrawSortItem *item = &queue->sort_items[i];
        DrawRequest *req = &queue->requests[item->request_index];
        const Submesh *submesh = &assets->submesh_table[item->submesh_id];
        SortKey key = unpack_sort_key(item->sort_key);

        queue->instance_data[i] = (ModelInstanceGpu){
            .model_id = req->model,
            .submesh_id = item->submesh_id,
            .material_id = submesh->material_id,
            .transform = queue->transforms[req->transform_index],
            .color = queue->colors[req->color_index],
        };
        
        /* Start new indirect command if state changes */
        if (key.pipeline_id != current_pipeline ||
            key.material_id != current_material ||
            key.submesh_id != current_submesh)
        {
            queue->indirect_commands[indirect_idx].firstInstance = i;
            queue->indirect_commands[indirect_idx].instanceCount = 1;
            current_pipeline = key.pipeline_id;
            current_material = key.material_id;
            current_submesh = key.submesh_id;
            indirect_idx++;
        } else {
            /* Batch with previous */
            queue->indirect_commands[indirect_idx - 1].instanceCount++;
        }
    }
    
    queue->instance_count = queue->sort_item_count;
    queue->indirect_count = indirect_idx;
}
```

---

## Problem 4: CPU Indirect Building (Scalability Ceiling at ~1000 objects)

### Before (Full Frame Sort Overhead)
```text
Per frame:
  - Build requests:        1000 objects
  - Expand to submeshes:   O(models + submeshes)
  - Sort compact records:  O(n log n)
  - Write instance stream: O(n)
  - Build indirect:        O(n)
  - Upload:                staged/ring copy

Cost: Growing with draw count
```

### After - Phase 1 (CPU remains, but better batching)
```c
/* Still CPU-driven, but better batching strategy */

void model_queue_prepare_frame(...) {
    /* Sort compact DrawSortItem records, not full instance payloads */
    qsort(queue->sort_items, queue->sort_item_count,
          sizeof(DrawSortItem), draw_call_cmp_sort_key);
    
    /* Write contiguous instance and indirect streams */
    write_instances_in_sorted_order(queue, assets);
    build_batched_indirect_commands(queue);
}
```

**Ceiling:** 1000s of objects (still acceptable for 2026 engines)

### Future - Phase 2 (GPU Compute, Unlimited Scale)
```glsl
/* Not implementing yet, but architecture must support this path */

// gpu_cull.comp
layout(set=0, binding=0) buffer DrawRequests {
    DrawRequest requests[];
};

void main() {
    uint id = gl_GlobalInvocationID.x;
    DrawRequest req = requests[id];
    
    // GPU fetches model/submesh/material data
    // GPU culls against frustum
    // GPU compacts valid draws
    // GPU writes indirect command
    // CPU just reads final count
    
    atomicAdd(indirect_count, 1);
}
```

**Ceiling:** Unlimited (tens of thousands)

**For now, Phase 1 design is forward-compatible:**
- Same data structures
- Same sort key format
- Same GPU buffer layout
- Can swap CPU loop for GPU compute later without API changes

---

## Problem 5: Path-Based Draw Submission (String Lookup Tax)

### Before (Hazardous)
```c
draw_model("assets/models/enemy.glb", transform);
```

**Problems:**
- Hash lookup every frame
- Path parsing
- Potential stalls on slow filesystem
- Can't distinguish identical paths in different folders
- No way to know if asset is loaded

### After (Handle-Based)
```c
/* Load phase (load screen, background thread, etc) */
typedef enum {
    MODEL_PLAYER,
    MODEL_ENEMY,
    MODEL_TERRAIN,
    MODEL_COUNT
} ModelId;

ModelHandle model_handles[MODEL_COUNT];

void load_models(ModelAssetSystem *assets) {
    model_handles[MODEL_PLAYER]  = model_asset_load_gltf(assets, "assets/models/player.glb");
    model_handles[MODEL_ENEMY]   = model_asset_load_gltf(assets, "assets/models/enemy.glb");
    model_handles[MODEL_TERRAIN] = model_asset_load_gltf(assets, "assets/models/terrain.glb");
}

/* Render frame (zero string operations) */
void render_frame(ModelRenderQueue *queue) {
    for (int i = 0; i < entity_count; i++) {
        Entity *ent = &entities[i];
        
        /* Direct handle, no lookup */
        model_queue_draw(
            queue,
            model_handles[ent->model_type],  /* Already a handle */
            ent->transform,
            ent->color
        );
    }
}
```

**Benefits:**
- Zero string operations in hot path
- Can validate handle upfront
- Can batch by model type easily
- Knows exactly which models are in flight

---

## Data-Oriented Frame Flow

The refactored model path should be readable as a sequence of array transforms:

```text
Loaded glTF
    -> append ModelData
    -> append Submesh records
    -> append MaterialData records
    -> schedule UploadRequest records
    -> upload packed GPU streams/tables

Per frame
    -> append DrawRequest records
    -> expand model ranges into DrawSortItem records
    -> sort DrawSortItem records
    -> write ModelInstanceGpu stream in sorted order
    -> build VkDrawIndirectCommand stream
    -> upload/prepare instance + indirect slices
    -> bind pipeline/descriptors once per batch
    -> issue indirect draws
```

This makes the CPU implementation easy to profile and makes the future GPU compute path straightforward. The compute path can consume the same tables and write the same compact instance/indirect streams.

## Recommended Memory Ownership

```text
ModelAssetSystem
    owns persistent model/submesh/material tables
    owns persistent GPU mesh slices
    owns upload scheduling
    owns cold path/debug metadata

ModelRenderQueue
    owns transient frame arrays
    owns sorted instance stream
    owns indirect command stream
    resets after submit/fence policy allows reuse

Renderer
    owns Vulkan device objects, descriptor tables, buffer pools, barriers
```

`ModelRenderQueue` should not free model assets. `ModelAssetSystem` should not know how many times a model is drawn this frame.

---

## Integration: How to Call This

```c
/* INIT (once) */
ModelAssetSystem asset_sys;
model_asset_init_gpu_buffers(
    &asset_sys, device, allocator,
    256 * 1024 * 1024,  /* 256MB vertex buffer */
    128 * 1024 * 1024   /* 128MB index buffer */
);

ModelRenderQueue render_queue;
render_queue.requests = malloc(sizeof(DrawRequest) * MAX_CONCURRENT_DRAWS);
render_queue.sort_items = malloc(sizeof(DrawSortItem) * MAX_CONCURRENT_DRAWS * MAX_SUBMESHES_PER_MODEL);
render_queue.instance_data = malloc(sizeof(ModelInstanceGpu) * MAX_CONCURRENT_DRAWS * MAX_SUBMESHES_PER_MODEL);
render_queue.indirect_commands = malloc(sizeof(VkDrawIndirectCommand) * MAX_CONCURRENT_DRAWS * MAX_SUBMESHES_PER_MODEL);

/* LOAD ASSETS (can be async, background thread) */
ModelHandle player_model = model_asset_load_gltf(&asset_sys, "player.glb");
ModelHandle enemy_model = model_asset_load_gltf(&asset_sys, "enemy.glb");

/* UPLOAD (once per frame, before rendering) */
model_asset_upload_pending(&asset_sys, staging_cmd);

/* PER FRAME GAME LOOP */
void frame_update() {
    model_queue_reset(&render_queue);
    
    /* Game logic: submit draws */
    for (int i = 0; i < entity_count; i++) {
        Entity *ent = &entities[i];
        model_queue_draw(
            &render_queue,
            player_model,  /* Handle, not path */
            ent->matrix,
            ent->color
        );
    }
    
    /* Prepare rendering */
    model_queue_prepare_frame(&render_queue, &asset_sys);
    
    /* Record render commands */
    model_queue_draw_all(&render_queue, cmd_buffer, &global_data);
}

/* SHUTDOWN */
model_asset_unload(&asset_sys, player_model);
model_asset_unload(&asset_sys, enemy_model);
```

---

## Migration Path: Current Code → Refactored

### Step 1: Extract Asset System (Don't Touch Rendering Yet)
1. Move all `ModelAssetSystem` code to new `model_asset.h/.c`
2. Split `GltfModelEntry` into `ModelData` + `Submesh`
3. Keep existing `draw3d()` working with new system
4. Test that assets still load

### Step 2: Extract Render Queue (Keep Asset System)
1. Move all queuing/sorting to `ModelRenderQueue`
2. Modify `draw3d()` to use `model_queue_draw()` internally
3. Modify `model_api_prepare_frame()` to call `model_queue_prepare_frame()`
4. Test that existing render still works

### Step 3: Add Multi-Mesh Support
1. Modify glTF loader to enumerate all primitives
2. Update sort key to include `submesh_id`
3. Test with multi-mesh models

### Step 4: Add Sort Key
1. Build complete `SortKey` struct
2. Sort by pipeline→material→submesh
3. Verify indirect command count matches new batching

### Step 5: Kill Path-Based API
1. Replace `draw_model(path)` with `model_queue_draw(handle)`
2. Require preloading models
3. Update all game code to use handles

**Each step is independent and testable.**

---

## Future: Phase 2 Compute (When You Hit Scaling Limits)

```c
/* This design supports GPU compute overlay when scale demands it */

/* GPU-side culling (Phase 2) */
void gpu_cull_compute(
    ModelRenderQueue *queue,
    const CullData *cull_data,
    VkCommandBuffer cmd
) {
    /* Dispatch: one thread per draw request */
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cull_compute);
    
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(CullPushConstants), &cull_push);
    
    vkCmdDispatch(cmd,
                  (queue->request_count + 63) / 64,  /* Workgroups */
                  1, 1);
    
    /* GPU now:
       - Culls against frustum
       - Compacts valid draws
       - Writes indirect command
       - Increments visible count
    */
}

/* Still call this, but no-op when GPU compute is active */
void model_queue_prepare_frame_cpu_fallback(...) {
    #ifdef GPU_CULL_ENABLED
        gpu_cull_compute(...);
    #else
        /* CPU path as before */
        qsort(...);
        build_indirect(...);
    #endif
}
```

**API doesn't change. Implementation swaps.**

---

## Summary: What This Buys You

| Problem | Solution | When You Need It |
|---------|----------|------------------|
| Fused systems | Split asset/queue | Before animation |
| Single mesh | Multi-mesh + submesh ranges | Before content |
| Bad sort | Pipeline→material→submesh | Before transparent objects |
| CPU tax | Phase 2 GPU compute | When rendering 10k+ objects |
| String tax | Handles + preload | Immediately |

**Do in this order:**
1. ✅ Split systems
2. ✅ Multi-mesh support
3. ✅ Improved sort key
4. ⏳ Handle-based submission (can do now or later)
5. ⏳ GPU compute (when you need it)

**Result:** Renderer that scales from hundreds to tens of thousands of objects without architecture thrashing.
