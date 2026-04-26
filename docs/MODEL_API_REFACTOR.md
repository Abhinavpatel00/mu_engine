# Model API Refactor Proposal

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
  ├─ load/unload        ├─ accept draw requests
  ├─ mesh buffers       ├─ sort instances
  ├─ materials          ├─ compact valid
  ├─ residency          └─ emit indirect
  └─ handles

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

typedef struct {
    uint32_t vertex_offset;     // In vertex buffer
    uint32_t index_offset;      // In index buffer
    uint32_t index_count;
    uint32_t material_id;       // Into material table
    AABB bounds;                // For future culling
} Submesh;

typedef struct {
    Submesh *submeshes;
    uint32_t submesh_count;
    uint32_t first_submesh_id;  // Global submesh table index
    AABB bounds;
} ModelData;

typedef struct {
    /* Long-lived storage */
    ModelData *models;
    uint32_t model_count;
    uint32_t model_capacity;
    
    /* Global submesh table */
    Submesh *submesh_table;
    uint32_t submesh_count;
    uint32_t submesh_capacity;
    
    /* GPU buffers (never resized mid-frame) */
    VkBuffer vertex_buffer;
    VkBuffer index_buffer;
    
    /* Offset allocators for incremental model loads */
    OffsetAllocator vertex_alloc;
    OffsetAllocator index_alloc;
    
    /* Pending uploads for next frame */
    struct {
        void *vertex_data;
        uint32_t vertex_size;
        
        void *index_data;
        uint32_t index_size;
    } pending;
    
} ModelAssetSystem;

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
    SubmeshId submesh;          /* NEW: track which submesh */
    Mat4 transform;
    Vec4 color;
    uint32_t sort_key;          /* NEW: complete sort key */
} DrawRequest;

typedef struct {
    /* Per-frame data */
    DrawRequest *requests;
    uint32_t request_count;
    uint32_t request_capacity;
    
    /* Sorted scratch space */
    DrawRequest *sorted;
    
    /* Instance buffer for GPU fetch */
    struct {
        uint32_t model_id;
        uint32_t submesh_id;
        Mat4 transform;
        Vec4 color;
        /* Pad to 128 bytes for alignment */
    } *instance_data;
    uint32_t instance_count;
    
    /* Indirect command buffer */
    VkDrawIndirectCommand *indirect_commands;
    uint32_t indirect_count;
    
    /* GPU buffers (resized per frame as needed) */
    VkBuffer instance_buffer;
    VkBuffer indirect_buffer;
    
} ModelRenderQueue;

/* Submit a draw request (called from game code per frame) */
void model_queue_draw(
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

### After (Split)
```c
/* Asset System (once at load) */
ModelHandle h = model_asset_load_gltf(asset_sys, "model.glb");

/* Per-frame rendering (in frame loop) */
for (int i = 0; i < instance_count; i++) {
    SubmeshId submesh = 0;  /* or loop all submeshes if multi-mesh */
    model_queue_draw(
        render_queue,
        h,
        submesh,
        transforms[i],
        colors[i]
    );
}
```

**Key benefit:** Asset system is decoupled from frame rate. Can load/unload models asynchronously in background thread without touching frame renderer.

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
Each Submesh has material_id, indices, vertices
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
    Submesh *submeshes;         /* Points into global submesh table */
    uint32_t submesh_count;
    uint32_t first_submesh_id;  /* Global submesh index */
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
    
    /* Store model metadata */
    ModelHandle h = sys->model_count++;
    sys->models[h] = (ModelData){
        .submeshes = &sys->submesh_table[first_submesh_id],
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

### After (Complete Sort Key)
```c
typedef struct {
    uint32_t pipeline_id : 3;      /* Opaque, transparent, alpha-mask */
    uint32_t material_id : 13;     /* 8192 materials max */
    uint32_t submesh_id : 16;      /* 65536 submeshes max */
} SortKey;

static inline SortKey build_sort_key(
    const DrawRequest *req,
    const ModelAssetSystem *assets
) {
    const ModelData *model = model_asset_get(assets, req->model);
    const Submesh *submesh = &model->submeshes[req->submesh_id];
    const Material *mat = material_get(submesh->material_id);
    
    uint32_t pipeline_id = 0;
    if (mat->flags & MATERIAL_TRANSPARENT)
        pipeline_id = 1;  /* Transparent pipeline */
    else if (mat->flags & MATERIAL_ALPHA_MASK)
        pipeline_id = 2;  /* Alpha-mask pipeline */
    /* else 0 = opaque */
    
    return (SortKey){
        .pipeline_id = pipeline_id,
        .material_id = submesh->material_id,
        .submesh_id = req->submesh_id,
    };
}

static int draw_call_cmp_sort_key(const void *a, const void *b) {
    const DrawRequest *da = (DrawRequest*)a;
    const DrawRequest *db = (DrawRequest*)b;
    
    uint32_t ka = *(uint32_t*)&da->sort_key;
    uint32_t kb = *(uint32_t*)&db->sort_key;
    
    return (int)ka - (int)kb;
}

void model_queue_prepare_frame(
    ModelRenderQueue *queue,
    const ModelAssetSystem *assets
) {
    /* Build sort keys for all requests */
    for (uint32_t i = 0; i < queue->request_count; i++) {
        DrawRequest *req = &queue->requests[i];
        req->sort_key = *(uint32_t*)&build_sort_key(req, assets);
    }
    
    /* Sort by key */
    qsort(queue->requests, queue->request_count, 
          sizeof(DrawRequest), draw_call_cmp_sort_key);
    
    /* Build indirect commands (now properly batched by material/submesh) */
    uint32_t indirect_idx = 0;
    uint32_t current_pipeline = UINT32_MAX;
    uint32_t current_material = UINT32_MAX;
    uint32_t current_submesh = UINT32_MAX;
    
    for (uint32_t i = 0; i < queue->request_count; i++) {
        DrawRequest *req = &queue->requests[i];
        SortKey *key = (SortKey*)&req->sort_key;
        
        /* Start new indirect command if state changes */
        if (key->pipeline_id != current_pipeline ||
            key->material_id != current_material ||
            key->submesh_id != current_submesh)
        {
            queue->indirect_commands[indirect_idx].firstInstance = i;
            queue->indirect_commands[indirect_idx].instanceCount = 1;
            current_pipeline = key->pipeline_id;
            current_material = key->material_id;
            current_submesh = key->submesh_id;
            indirect_idx++;
        } else {
            /* Batch with previous */
            queue->indirect_commands[indirect_idx - 1].instanceCount++;
        }
    }
    
    queue->indirect_count = indirect_idx;
}
```

---

## Problem 4: CPU Indirect Building (Scalability Ceiling at ~1000 objects)

### Before (Full Frame Sort Overhead)
```text
Per frame:
  - Build requests: 1000 objects
  - Sort:           O(n log n) = ~9000 comparisons
  - Compact:        O(n)
  - Build indirect: O(n)
  - Upload:         Stall GPU for buffer write

Cost: Growing with draw count
```

### After - Phase 1 (CPU remains, but better batching)
```c
/* Still CPU-driven, but better batching strategy */

void model_queue_prepare_frame(...) {
    /* Batch by material, not just model */
    /* Same O(n log n) sort, but better spatial coherence */
    qsort(queue->requests, queue->request_count, 
          sizeof(DrawRequest), draw_call_cmp_sort_key);
    
    /* Build multi-draw indirect more efficiently */
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
            0,                                /* Submesh ID */
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
render_queue.instance_buffer = create_gpu_buffer(...);
render_queue.indirect_buffer = create_gpu_buffer(...);

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
            0,             /* Submesh 0 (or loop all submeshes) */
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
