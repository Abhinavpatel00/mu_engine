#include "gltf_gpu_mesh.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"
#include "passes.h"

// New modular subsystems
#include "model_assets.h"
#include "model_instances.h"
#include "animation_system.h"
#include "model_render.h"

typedef struct AABB
{
    float min[3];
    float max[3];
} AABB;

typedef struct Submesh
{
    BufferSlice position_slice;
    BufferSlice uv_slice;
    BufferSlice packed_slice;
    BufferSlice index_slice;
    uint32_t    vertex_offset;
    uint32_t    index_offset;
    uint32_t    index_count;
    uint32_t    vertex_count;
    uint32_t    material_id;
    uint32_t    flags;
    AABB        bounds;
} Submesh;

typedef struct ModelData
{
    uint32_t first_submesh_id;
    uint32_t submesh_count;
    uint32_t first_material_id;
    uint32_t material_count;
    uint32_t first_clip_id;
    uint32_t clip_count;
    uint8_t  has_skeleton;
    uint8_t  has_skinning;
    uint8_t  _pad0;
    uint8_t  _pad1;
    AABB     bounds;
} ModelData;

typedef struct AnimationClipData
{
    char* name;
    float duration;
} AnimationClipData;

typedef struct MaterialData
{
    uint32_t base_color_texture_id;
    uint32_t normal_texture_id;
    uint32_t orm_texture_id;
    uint32_t sampler_id;
    float    base_color_factor[4];
    float    metallic_factor;
    float    roughness_factor;
    uint32_t flags;
} MaterialData;

typedef struct UploadRequest
{
    BufferSlice dst;
    void*       src;
    uint32_t    size;
    uint32_t    alignment;
    uint32_t    stage_flags;
} UploadRequest;

typedef struct DrawRequest
{
    ModelHandle model;
    uint32_t    transform_index;
    uint32_t    color_index;
    uint32_t    flags;
} DrawRequest;

typedef struct DrawSortItem
{
    uint64_t sort_key;
    uint32_t request_index;
    uint32_t submesh_id;
} DrawSortItem;

typedef struct ModelInstanceGpu
{
    uint32_t model_id;
    uint32_t submesh_id;
    uint32_t material_id;
    uint32_t flags;
    VkDeviceAddress pos_ptr;
    float    transform[4][4];
    float    color[4];
} ModelInstanceGpu;

typedef struct SubmeshMetaGpu
{
    VkDeviceAddress pos_ptr;
    VkDeviceAddress uv_ptr;
    VkDeviceAddress idx_ptr;
    uint32_t        index_count;
    uint32_t        material_id;
    uint32_t        _pad0;
    uint32_t        _pad1;
} SubmeshMetaGpu;

typedef struct MaterialGpu
{
    uint32_t base_color_texture_id;
    uint32_t sampler_id;
    uint32_t _pad0;
    uint32_t _pad1;
    float    base_color_factor[4];
} MaterialGpu;

typedef struct ModelIndirectPush
{
    VkDeviceAddress submesh_table_ptr;
    VkDeviceAddress material_table_ptr;
    VkDeviceAddress instance_ptr;
    uint64_t        pad0;
    float           view_proj[4][4];
} ModelIndirectPush;

typedef struct SkinningPush
{
    VkDeviceAddress src_vertex_ptr;
    VkDeviceAddress dst_vertex_ptr;
    VkDeviceAddress palette_ptr;
    uint32_t        vertex_count;
    uint32_t        joint_count;
    uint32_t        _pad0;
    uint32_t        _pad1;
} SkinningPush;

typedef struct ModelAssetSystem
{
    mu_id_pool model_id_pool;

    ModelData* models;
    uint32_t   model_count;
    uint32_t   model_capacity;
    uint32_t*  active_model_ids;
    uint32_t   active_model_count;

    Submesh*  submesh_table;
    uint32_t  submesh_count;
    uint32_t  submesh_capacity;

    MaterialData* material_table;
    MaterialGpu*  material_gpu_table;
    uint32_t      material_count;
    uint32_t      material_capacity;

    AnimationClipData* clip_table;
    uint32_t           clip_count;
    uint32_t           clip_capacity;

    SubmeshMetaGpu* submesh_meta_table;

    char** debug_paths;

    UploadRequest* pending_uploads;
    uint32_t       pending_upload_count;
    uint32_t       pending_upload_capacity;

    BufferSlice submesh_meta_slice;
    BufferSlice material_gpu_slice;

    bool submesh_meta_dirty;
    bool material_dirty;
} ModelAssetSystem;

typedef struct ModelInstanceData
{
    ModelHandle          model;
    float                transform[4][4];
    float                color[4];
    AnimationState       anim;
    AnimationPlaybackMode playback_mode;
    bool                 active;
    
    // GPU skinning buffers (allocated only if model has skinning)
    BufferSlice          skinned_vertex_buffer;
    BufferSlice          palette_buffer;
    bool                 palette_dirty;
} ModelInstanceData;

typedef struct ModelInstanceSystem
{
    mu_id_pool         instance_id_pool;
    ModelInstanceData* instances;
    uint32_t*          active_instance_ids;
    uint32_t           active_instance_count;
    uint32_t           instance_capacity;
    float              frame_dt;
} ModelInstanceSystem;

typedef struct ModelRenderQueue
{
    DrawRequest* requests;
    uint32_t     request_count;
    uint32_t     request_capacity;

    float (*transforms)[4][4];
    float (*colors)[4];
    uint32_t transform_count;
    uint32_t color_count;

    DrawSortItem* sort_items;
    uint32_t      sort_item_count;
    uint32_t      sort_item_capacity;

    ModelInstanceGpu* instance_data;
    uint32_t          instance_count;
    uint32_t          instance_capacity;

    VkDrawIndirectCommand* indirect_commands;
    uint32_t               indirect_count;
    uint32_t               indirect_capacity;

    BufferSlice instance_slice;
    BufferSlice indirect_slice;

    bool  prepared;
    float frame_view_proj[4][4];
} ModelRenderQueue;

typedef struct ModelApiState
{
    bool initialized;

    ModelAssetSystem assets;
    ModelRenderQueue queue;
    ModelInstanceSystem instance_system;
    AnimationDebugMode  debug_mode;
    
    PipelineID       skinning_pipeline;
    Renderer*        renderer;
} ModelApiState;

typedef struct ModelMaterialSource
{
    char* base_color_tex;
    char* normal_tex;
    char* orm_tex;
    float base_color_factor[4];
    float metallic_factor;
    float roughness_factor;
    uint32_t flags;
} ModelMaterialSource;

typedef struct ModelSubmeshSource
{
    uint32_t material_index;
    uint32_t vertex_offset;
    uint32_t vertex_count;
    uint32_t index_offset;
    uint32_t index_count;
    AABB     bounds;
    uint32_t flags;
} ModelSubmeshSource;

typedef struct ModelSource
{
    float*    positions_xyz;
    float*    uv0_xy;
    float*    normals_xyz;
    float*    tangents_xyzw;
    uint16_t* joints_u16;
    uint16_t* weights_u16;
    uint32_t* indices;
    uint32_t  vertex_count;
    uint32_t  index_count;

    bool has_normal;
    bool has_tangent;
    bool has_joints;
    bool has_weights;

    ModelMaterialSource* materials;
    uint32_t             material_count;

    ModelSubmeshSource* submeshes;
    uint32_t            submesh_count;

    AnimationClipData* clips;
    uint32_t           clip_count;

    bool has_skeleton;
    bool has_skinning;

    AABB bounds;
} ModelSource;

typedef struct MeshxMaterialParse
{
    bool seen_name;
    bool seen_base_color_tex;
    bool seen_normal_tex;
    bool seen_orm_tex;
    bool seen_base_color_factor;
    bool seen_metallic;
    bool seen_roughness;
    bool seen_alpha_mode;
} MeshxMaterialParse;

typedef struct MeshxSubmeshParse
{
    bool seen_material;
    bool seen_vertex_offset;
    bool seen_vertex_count;
    bool seen_index_offset;
    bool seen_index_count;
    bool seen_bounds_min;
    bool seen_bounds_max;
} MeshxSubmeshParse;

// Include helper functions for vertex packing and normal/tangent computation
#include "gltf_gpu_mesh_helpers.c"

static ModelApiState g_model_api = {0};

#define MODEL_INVALID_TEXTURE_ID UINT32_MAX
#define MESHX_MAX_TOKENS 24

static bool model_handle_valid(const ModelAssetSystem* sys, ModelHandle h)
{
    return h != MODEL_HANDLE_INVALID && h < sys->model_capacity && mu_id_pool_is_id(&sys->model_id_pool, h);
}

static void mat4_identity(float out[4][4])
{
    memset(out, 0, sizeof(float[4][4]));
    out[0][0] = 1.0f;
    out[1][1] = 1.0f;
    out[2][2] = 1.0f;
    out[3][3] = 1.0f;
}

static AnimationState animation_state_default(void)
{
    AnimationState s = {0};
    s.clip = ANIMATION_CLIP_INVALID;
    s.time = 0.0f;
    s.speed = 1.0f;
    s.weight = 1.0f;
    s.playing = false;
    s.paused = false;
    s.loop = true;
    return s;
}

static bool model_instance_valid(const ModelApiState* api, ModelInstanceHandle instance)
{
    if(!api)
        return false;

    const ModelInstanceSystem* sys = &api->instance_system;
    return instance != MODEL_INSTANCE_HANDLE_INVALID
           && instance < sys->instance_capacity
           && mu_id_pool_is_id(&sys->instance_id_pool, instance)
           && sys->instances[instance].active;
}

static bool model_instance_remove_active_id(ModelInstanceSystem* sys, uint32_t id)
{
    for(uint32_t i = 0; i < sys->active_instance_count; ++i)
    {
        if(sys->active_instance_ids[i] == id)
        {
            sys->active_instance_ids[i] = sys->active_instance_ids[sys->active_instance_count - 1u];
            sys->active_instance_count--;
            return true;
        }
    }
    return false;
}

static bool model_instance_system_init(ModelInstanceSystem* sys, uint32_t instance_capacity)
{
    if(!sys || instance_capacity == 0)
        return false;

    memset(sys, 0, sizeof(*sys));
    mu_id_pool_init(&sys->instance_id_pool, instance_capacity);

    sys->instance_capacity = instance_capacity;
    sys->instances = (ModelInstanceData*)calloc(instance_capacity, sizeof(ModelInstanceData));
    sys->active_instance_ids = (uint32_t*)calloc(instance_capacity, sizeof(uint32_t));
    if(!sys->instances || !sys->active_instance_ids)
        return false;

    return true;
}

static void model_instance_system_shutdown(ModelInstanceSystem* sys)
{
    if(!sys)
        return;

    free(sys->active_instance_ids);
    free(sys->instances);

    if(sys->instance_id_pool.ranges)
        mu_id_pool_deinit(&sys->instance_id_pool);

    memset(sys, 0, sizeof(*sys));
}

static char* strdup_owned(const char* src)
{
    if(!src)
        return NULL;

    size_t len = strlen(src);
    char* out = (char*)malloc(len + 1u);
    if(!out)
        return NULL;

    memcpy(out, src, len + 1u);
    return out;
}

static void aabb_make_default(AABB* out)
{
    out->min[0] = out->min[1] = out->min[2] = 0.0f;
    out->max[0] = out->max[1] = out->max[2] = 0.0f;
}

static bool resolve_texture_path(const char* base_path, const char* uri, char* out_path, size_t out_size)
{
    if(!base_path || !uri || !out_path || out_size == 0)
        return false;

    if(strstr(uri, "://") != NULL)
        return false;

    if(uri[0] == '/')
    {
        snprintf(out_path, out_size, "%s", uri);
        return true;
    }

    const char* slash = strrchr(base_path, '/');
    if(!slash)
    {
        snprintf(out_path, out_size, "%s", uri);
        return true;
    }

    size_t prefix_len = (size_t)(slash - base_path + 1);
    size_t uri_len = strlen(uri);
    if(prefix_len + uri_len + 1u > out_size)
        return false;

    memcpy(out_path, base_path, prefix_len);
    memcpy(out_path + prefix_len, uri, uri_len + 1u);
    return true;
}

static bool file_exists(const char* path)
{
    if(!path)
        return false;
    FILE* f = fopen(path, "rb");
    if(!f)
        return false;
    fclose(f);
    return true;
}

static bool path_try_meshx_sidecar(const char* source_path, char* out_path, size_t out_size)
{
    if(!source_path || !out_path || out_size == 0)
        return false;

    size_t len = strlen(source_path);
    if(len + 1u > out_size)
        return false;

    memcpy(out_path, source_path, len + 1u);

    char* dot = strrchr(out_path, '.');
    if(!dot)
        return false;

    snprintf(dot, out_size - (size_t)(dot - out_path), ".meshx");
    return file_exists(out_path);
}

static int draw_sort_item_cmp(const void* a, const void* b)
{
    const DrawSortItem* ia = (const DrawSortItem*)a;
    const DrawSortItem* ib = (const DrawSortItem*)b;
    if(ia->sort_key < ib->sort_key)
        return -1;
    if(ia->sort_key > ib->sort_key)
        return 1;
    return 0;
}

static int split_tokens_inplace(char* line, char* tokens[MESHX_MAX_TOKENS])
{
    int count = 0;
    char* p = line;

    while(*p && count < MESHX_MAX_TOKENS)
    {
        while(*p && isspace((unsigned char)*p))
            ++p;
        if(!*p)
            break;

        if(*p == '"')
        {
            ++p;
            tokens[count++] = p;
            while(*p && *p != '"')
                ++p;
            if(*p == '"')
            {
                *p = '\0';
                ++p;
            }
            continue;
        }

        tokens[count++] = p;
        while(*p && !isspace((unsigned char)*p))
            ++p;
        if(*p)
        {
            *p = '\0';
            ++p;
        }
    }

    return count;
}

static bool model_asset_push_upload(ModelAssetSystem* sys, BufferSlice dst, const void* src, uint32_t size, uint32_t alignment, uint32_t stage_flags)
{
    if(!sys || !src || size == 0)
        return false;

    if(sys->pending_upload_count == sys->pending_upload_capacity)
    {
        uint32_t new_cap = sys->pending_upload_capacity ? sys->pending_upload_capacity * 2u : 64u;
        UploadRequest* new_arr = (UploadRequest*)realloc(sys->pending_uploads, (size_t)new_cap * sizeof(UploadRequest));
        if(!new_arr)
            return false;
        sys->pending_uploads = new_arr;
        sys->pending_upload_capacity = new_cap;
    }

    void* copy = malloc(size);
    if(!copy)
        return false;
    memcpy(copy, src, size);

    UploadRequest* req = &sys->pending_uploads[sys->pending_upload_count++];
    req->dst = dst;
    req->src = copy;
    req->size = size;
    req->alignment = alignment;
    req->stage_flags = stage_flags;
    return true;
}

static void model_source_free(ModelSource* src)
{
    if(!src)
        return;

    free(src->positions_xyz);
    free(src->uv0_xy);
    free(src->normals_xyz);
    free(src->tangents_xyzw);
    free(src->joints_u16);
    free(src->weights_u16);
    free(src->indices);

    if(src->materials)
    {
        for(uint32_t i = 0; i < src->material_count; ++i)
        {
            free(src->materials[i].base_color_tex);
            free(src->materials[i].normal_tex);
            free(src->materials[i].orm_tex);
        }
    }
    free(src->materials);
    free(src->submeshes);

    if(src->clips)
    {
        for(uint32_t i = 0; i < src->clip_count; ++i)
            free(src->clips[i].name);
    }
    free(src->clips);

    memset(src, 0, sizeof(*src));
}

static ModelHandle model_asset_find_by_path(const ModelAssetSystem* sys, const char* path)
{
    if(!sys || !path)
        return MODEL_HANDLE_INVALID;

    for(uint32_t i = 0; i < sys->model_capacity; ++i)
    {
        if(model_handle_valid(sys, i) && sys->debug_paths[i] && strcmp(sys->debug_paths[i], path) == 0)
            return i;
    }
    return MODEL_HANDLE_INVALID;
}

static bool model_asset_system_init(ModelAssetSystem* sys, uint32_t max_models)
{
    if(!sys || max_models == 0)
        return false;

    memset(sys, 0, sizeof(*sys));

    mu_id_pool_init(&sys->model_id_pool, max_models);

    sys->model_capacity = max_models;
    sys->models = (ModelData*)calloc(max_models, sizeof(ModelData));
    sys->debug_paths = (char**)calloc(max_models, sizeof(char*));
    sys->active_model_ids = (uint32_t*)calloc(max_models, sizeof(uint32_t));

    sys->submesh_capacity = max_models * 16u;
    if(sys->submesh_capacity < 64u)
        sys->submesh_capacity = 64u;
    sys->submesh_table = (Submesh*)calloc(sys->submesh_capacity, sizeof(Submesh));
    sys->submesh_meta_table = (SubmeshMetaGpu*)calloc(sys->submesh_capacity, sizeof(SubmeshMetaGpu));

    sys->material_capacity = max_models * 8u;
    if(sys->material_capacity < 64u)
        sys->material_capacity = 64u;
    sys->material_table = (MaterialData*)calloc(sys->material_capacity, sizeof(MaterialData));
    sys->material_gpu_table = (MaterialGpu*)calloc(sys->material_capacity, sizeof(MaterialGpu));

    sys->clip_capacity = max_models * 32u;
    if(sys->clip_capacity < 64u)
        sys->clip_capacity = 64u;
    sys->clip_table = (AnimationClipData*)calloc(sys->clip_capacity, sizeof(AnimationClipData));

    if(!sys->models || !sys->debug_paths || !sys->active_model_ids || !sys->submesh_table || !sys->submesh_meta_table
       || !sys->material_table || !sys->material_gpu_table || !sys->clip_table)
    {
        return false;
    }

    sys->submesh_meta_slice = buffer_pool_alloc(&renderer.gpu_pool, (VkDeviceSize)sys->submesh_capacity * sizeof(SubmeshMetaGpu), 16);
    sys->material_gpu_slice = buffer_pool_alloc(&renderer.gpu_pool, (VkDeviceSize)sys->material_capacity * sizeof(MaterialGpu), 16);

    if(sys->submesh_meta_slice.buffer == VK_NULL_HANDLE || sys->material_gpu_slice.buffer == VK_NULL_HANDLE)
        return false;

    sys->submesh_meta_dirty = true;
    sys->material_dirty = true;
    return true;
}

static void model_asset_system_shutdown(ModelAssetSystem* sys)
{
    if(!sys)
        return;

    if(sys->submesh_meta_slice.buffer != VK_NULL_HANDLE)
        buffer_pool_free(sys->submesh_meta_slice);
    if(sys->material_gpu_slice.buffer != VK_NULL_HANDLE)
        buffer_pool_free(sys->material_gpu_slice);

    for(uint32_t i = 0; i < sys->model_capacity; ++i)
        free(sys->debug_paths ? sys->debug_paths[i] : NULL);

    for(uint32_t i = 0; i < sys->submesh_count; ++i)
    {
        if(sys->submesh_table[i].position_slice.buffer != VK_NULL_HANDLE)
            buffer_pool_free(sys->submesh_table[i].position_slice);
        if(sys->submesh_table[i].uv_slice.buffer != VK_NULL_HANDLE)
            buffer_pool_free(sys->submesh_table[i].uv_slice);
        if(sys->submesh_table[i].index_slice.buffer != VK_NULL_HANDLE)
            buffer_pool_free(sys->submesh_table[i].index_slice);
    }

    for(uint32_t i = 0; i < sys->material_count; ++i)
    {
        if(sys->material_table[i].base_color_texture_id != MODEL_INVALID_TEXTURE_ID)
            destroy_texture(&renderer, sys->material_table[i].base_color_texture_id);
        if(sys->material_table[i].normal_texture_id != MODEL_INVALID_TEXTURE_ID)
            destroy_texture(&renderer, sys->material_table[i].normal_texture_id);
        if(sys->material_table[i].orm_texture_id != MODEL_INVALID_TEXTURE_ID)
            destroy_texture(&renderer, sys->material_table[i].orm_texture_id);
    }

    for(uint32_t i = 0; i < sys->clip_count; ++i)
        free(sys->clip_table[i].name);

    for(uint32_t i = 0; i < sys->pending_upload_count; ++i)
        free(sys->pending_uploads[i].src);

    free(sys->pending_uploads);
    free(sys->material_gpu_table);
    free(sys->material_table);
    free(sys->clip_table);
    free(sys->submesh_meta_table);
    free(sys->submesh_table);
    free(sys->active_model_ids);
    free(sys->debug_paths);
    free(sys->models);

    if(sys->model_id_pool.ranges)
        mu_id_pool_deinit(&sys->model_id_pool);

    memset(sys, 0, sizeof(*sys));
}

static bool model_queue_init(ModelRenderQueue* queue, uint32_t request_capacity)
{
    if(!queue || request_capacity == 0)
        return false;

    memset(queue, 0, sizeof(*queue));

    queue->request_capacity = request_capacity;
    queue->sort_item_capacity = request_capacity * 16u;
    if(queue->sort_item_capacity < request_capacity)
        return false;

    queue->instance_capacity = queue->sort_item_capacity;
    queue->indirect_capacity = queue->sort_item_capacity;

    queue->requests = (DrawRequest*)calloc(queue->request_capacity, sizeof(DrawRequest));
    queue->transforms = (float(*)[4][4])calloc(queue->request_capacity, sizeof(float[4][4]));
    queue->colors = (float(*)[4])calloc(queue->request_capacity, sizeof(float[4]));
    queue->sort_items = (DrawSortItem*)calloc(queue->sort_item_capacity, sizeof(DrawSortItem));
    queue->instance_data = (ModelInstanceGpu*)calloc(queue->instance_capacity, sizeof(ModelInstanceGpu));
    queue->indirect_commands = (VkDrawIndirectCommand*)calloc(queue->indirect_capacity, sizeof(VkDrawIndirectCommand));

    if(!queue->requests || !queue->transforms || !queue->colors || !queue->sort_items || !queue->instance_data || !queue->indirect_commands)
        return false;

    queue->instance_slice = buffer_pool_alloc(&renderer.gpu_pool, (VkDeviceSize)queue->instance_capacity * sizeof(ModelInstanceGpu), 16);
    queue->indirect_slice = buffer_pool_alloc(&renderer.gpu_pool, (VkDeviceSize)queue->indirect_capacity * sizeof(VkDrawIndirectCommand), 16);

    if(queue->instance_slice.buffer == VK_NULL_HANDLE || queue->indirect_slice.buffer == VK_NULL_HANDLE)
        return false;

    return true;
}

static void model_queue_shutdown(ModelRenderQueue* queue)
{
    if(!queue)
        return;

    if(queue->instance_slice.buffer != VK_NULL_HANDLE)
        buffer_pool_free(queue->instance_slice);
    if(queue->indirect_slice.buffer != VK_NULL_HANDLE)
        buffer_pool_free(queue->indirect_slice);

    free(queue->indirect_commands);
    free(queue->instance_data);
    free(queue->sort_items);
    free(queue->colors);
    free(queue->transforms);
    free(queue->requests);

    memset(queue, 0, sizeof(*queue));
}

static bool meshx_reserve_vertices(ModelSource* out_src, uint32_t vertex_count)
{
    if(vertex_count == 0)
        return false;

    out_src->positions_xyz = (float*)realloc(out_src->positions_xyz, (size_t)vertex_count * 3u * sizeof(float));
    out_src->uv0_xy = (float*)realloc(out_src->uv0_xy, (size_t)vertex_count * 2u * sizeof(float));
    if(out_src->has_normal)
        out_src->normals_xyz = (float*)realloc(out_src->normals_xyz, (size_t)vertex_count * 3u * sizeof(float));
    if(out_src->has_tangent)
        out_src->tangents_xyzw = (float*)realloc(out_src->tangents_xyzw, (size_t)vertex_count * 4u * sizeof(float));
    if(out_src->has_joints)
        out_src->joints_u16 = (uint16_t*)realloc(out_src->joints_u16, (size_t)vertex_count * 4u * sizeof(uint16_t));
    if(out_src->has_weights)
        out_src->weights_u16 = (uint16_t*)realloc(out_src->weights_u16, (size_t)vertex_count * 4u * sizeof(uint16_t));

    return out_src->positions_xyz && out_src->uv0_xy && (!out_src->has_normal || out_src->normals_xyz) && (!out_src->has_tangent || out_src->tangents_xyzw)
           && (!out_src->has_joints || out_src->joints_u16) && (!out_src->has_weights || out_src->weights_u16);
}

static bool meshx_reserve_indices(ModelSource* out_src, uint32_t index_count)
{
    if(index_count == 0)
        return false;

    out_src->indices = (uint32_t*)realloc(out_src->indices, (size_t)index_count * sizeof(uint32_t));
    return out_src->indices != NULL;
}

static bool meshx_append_material(ModelSource* out_src)
{
    uint32_t next = out_src->material_count + 1u;
    ModelMaterialSource* arr = (ModelMaterialSource*)realloc(out_src->materials, (size_t)next * sizeof(ModelMaterialSource));
    if(!arr)
        return false;

    out_src->materials = arr;
    memset(&out_src->materials[out_src->material_count], 0, sizeof(ModelMaterialSource));
    out_src->materials[out_src->material_count].base_color_factor[0] = 1.0f;
    out_src->materials[out_src->material_count].base_color_factor[1] = 1.0f;
    out_src->materials[out_src->material_count].base_color_factor[2] = 1.0f;
    out_src->materials[out_src->material_count].base_color_factor[3] = 1.0f;
    out_src->materials[out_src->material_count].metallic_factor = 1.0f;
    out_src->materials[out_src->material_count].roughness_factor = 1.0f;
    out_src->material_count = next;
    return true;
}

static bool meshx_append_submesh(ModelSource* out_src)
{
    uint32_t next = out_src->submesh_count + 1u;
    ModelSubmeshSource* arr = (ModelSubmeshSource*)realloc(out_src->submeshes, (size_t)next * sizeof(ModelSubmeshSource));
    if(!arr)
        return false;

    out_src->submeshes = arr;
    memset(&out_src->submeshes[out_src->submesh_count], 0, sizeof(ModelSubmeshSource));
    out_src->submesh_count = next;
    return true;
}

static bool meshx_append_clip(ModelSource* out_src)
{
    uint32_t next = out_src->clip_count + 1u;
    AnimationClipData* arr = (AnimationClipData*)realloc(out_src->clips, (size_t)next * sizeof(AnimationClipData));
    if(!arr)
        return false;

    out_src->clips = arr;
    memset(&out_src->clips[out_src->clip_count], 0, sizeof(AnimationClipData));
    out_src->clips[out_src->clip_count].duration = 0.0f;
    out_src->clip_count = next;
    return true;
}

static bool model_source_from_meshx(const char* path, ModelSource* out_src)
{
    char* text = NULL;
    if(!helpers_read_text_file(path, &text, NULL))
        return false;

    enum Scope
    {
        scope_root,
        scope_model_bounds,
        scope_vertex_layout,
        scope_material,
        scope_submesh,
        scope_submesh_bounds,
        scope_animations,
        scope_skip_block,
        scope_vertices,
        scope_indices,
    };

    ModelSource src = {0};
    MeshxMaterialParse current_material_parse = {0};
    MeshxSubmeshParse current_submesh_parse = {0};
    bool have_model_bounds_min = false;
    bool have_model_bounds_max = false;
    bool have_layout_position = false;
    bool have_layout_normal = false;
    bool have_layout_uv0 = false;
    bool have_layout_tangent = false;
    bool have_layout_joints = false;
    bool have_layout_weights = false;

    enum Scope scope = scope_root;
    enum Scope pending_scope = scope_root;
    uint32_t current_material = UINT32_MAX;
    uint32_t current_submesh = UINT32_MAX;

    uint32_t vertex_write_index = 0;
    uint32_t vertex_capacity = 0;
    uint32_t index_write_index = 0;
    uint32_t index_capacity = 0;
    int animations_brace_depth = 0;
    int skip_block_brace_depth = 0;
    uint32_t current_clip = UINT32_MAX;

    char* cursor = text;
    bool ok = true;

    while(ok)
    {
        char* line = helpers_next_line(&cursor);
        if(!line)
            break;

        char* hash = strchr(line, '#');
        if(hash)
            *hash = '\0';
        helpers_trim_ascii(line);
        if(line[0] == '\0')
            continue;

        char* tokens[MESHX_MAX_TOKENS] = {0};
        int token_count = split_tokens_inplace(line, tokens);
        if(token_count == 0)
            continue;

        if(strcmp(tokens[0], "{") == 0)
        {
            if(scope == scope_animations)
            {
                animations_brace_depth++;
                continue;
            }

            if(scope == scope_skip_block)
            {
                skip_block_brace_depth++;
                continue;
            }

            if(pending_scope == scope_root)
            {
                ok = false;
                break;
            }
            scope = pending_scope;
            if(scope == scope_animations)
                animations_brace_depth = 1;
            if(scope == scope_skip_block)
                skip_block_brace_depth = 1;
            pending_scope = scope_root;
            continue;
        }

        if(strcmp(tokens[0], "}") == 0)
        {
            if(scope == scope_animations)
            {
                if(animations_brace_depth > 0)
                    animations_brace_depth--;
                if(animations_brace_depth <= 1)
                    current_clip = UINT32_MAX;
                if(animations_brace_depth == 0)
                    scope = scope_root;
                continue;
            }

            if(scope == scope_skip_block)
            {
                if(skip_block_brace_depth > 0)
                    skip_block_brace_depth--;
                if(skip_block_brace_depth == 0)
                    scope = scope_root;
                continue;
            }

            if(scope == scope_submesh_bounds)
            {
                scope = scope_submesh;
                continue;
            }

            if(scope == scope_material)
            {
                if(!(current_material_parse.seen_name && current_material_parse.seen_base_color_tex && current_material_parse.seen_normal_tex
                     && current_material_parse.seen_orm_tex && current_material_parse.seen_base_color_factor
                     && current_material_parse.seen_metallic && current_material_parse.seen_roughness
                     && current_material_parse.seen_alpha_mode))
                {
                    ok = false;
                    break;
                }
            }

            if(scope == scope_submesh)
            {
                if(!(current_submesh_parse.seen_material && current_submesh_parse.seen_vertex_offset && current_submesh_parse.seen_vertex_count
                     && current_submesh_parse.seen_index_offset && current_submesh_parse.seen_index_count
                     && current_submesh_parse.seen_bounds_min && current_submesh_parse.seen_bounds_max))
                {
                    ok = false;
                    break;
                }
            }

            scope = scope_root;
            continue;
        }

        if(scope == scope_root)
        {
            if(strcmp(tokens[0], "mesh") == 0)
                continue;
            if(strcmp(tokens[0], "bounds") == 0)
            {
                pending_scope = scope_model_bounds;
                continue;
            }
            if(strcmp(tokens[0], "vertex_layout") == 0)
            {
                pending_scope = scope_vertex_layout;
                continue;
            }
            if(strcmp(tokens[0], "material") == 0)
            {
                if(token_count < 2 || !meshx_append_material(&src))
                {
                    ok = false;
                    break;
                }
                current_material = src.material_count - 1u;
                memset(&current_material_parse, 0, sizeof(current_material_parse));
                pending_scope = scope_material;
                continue;
            }
            if(strcmp(tokens[0], "submesh") == 0)
            {
                if(token_count < 2 || !meshx_append_submesh(&src))
                {
                    ok = false;
                    break;
                }
                current_submesh = src.submesh_count - 1u;
                memset(&current_submesh_parse, 0, sizeof(current_submesh_parse));
                pending_scope = scope_submesh;
                continue;
            }
            if(strcmp(tokens[0], "animations") == 0)
            {
                pending_scope = scope_animations;
                continue;
            }
            if(strcmp(tokens[0], "skeletons") == 0)
            {
                src.has_skeleton = true;
                pending_scope = scope_skip_block;
                continue;
            }
            if(strcmp(tokens[0], "skins") == 0)
            {
                src.has_skinning = true;
                pending_scope = scope_skip_block;
                continue;
            }
            if(strcmp(tokens[0], "vertices") == 0)
            {
                // commit layout choices to source so reserves allocate correctly
                src.has_normal = have_layout_normal;
                src.has_tangent = have_layout_tangent;
                src.has_joints = have_layout_joints;
                src.has_weights = have_layout_weights;
                pending_scope = scope_vertices;
                continue;
            }
            if(strcmp(tokens[0], "indices") == 0)
            {
                pending_scope = scope_indices;
                continue;
            }

            ok = false;
            break;
        }

        if(scope == scope_animations)
        {
            if(animations_brace_depth == 1 && strcmp(tokens[0], "clip") == 0)
            {
                if(!meshx_append_clip(&src))
                {
                    ok = false;
                    break;
                }
                current_clip = src.clip_count - 1u;
                continue;
            }

            if(animations_brace_depth == 2 && current_clip != UINT32_MAX)
            {
                AnimationClipData* clip = &src.clips[current_clip];
                if(strcmp(tokens[0], "name") == 0 && token_count == 2)
                {
                    free(clip->name);
                    clip->name = strdup_owned(tokens[1]);
                    if(!clip->name)
                    {
                        ok = false;
                        break;
                    }
                    continue;
                }

                if(strcmp(tokens[0], "duration") == 0 && token_count == 2)
                {
                    if(!helpers_parse_f32(tokens[1], &clip->duration))
                    {
                        ok = false;
                        break;
                    }
                    continue;
                }
            }

            continue;
        }

        if(scope == scope_skip_block)
            continue;

        if(scope == scope_model_bounds)
        {
            if(token_count != 4)
            {
                ok = false;
                break;
            }

            if(strcmp(tokens[0], "min") == 0)
            {
                ok = helpers_parse_f32(tokens[1], &src.bounds.min[0])
                     && helpers_parse_f32(tokens[2], &src.bounds.min[1])
                     && helpers_parse_f32(tokens[3], &src.bounds.min[2]);
                have_model_bounds_min = ok;
                if(!ok)
                    break;
                continue;
            }

            if(strcmp(tokens[0], "max") == 0)
            {
                ok = helpers_parse_f32(tokens[1], &src.bounds.max[0])
                     && helpers_parse_f32(tokens[2], &src.bounds.max[1])
                     && helpers_parse_f32(tokens[3], &src.bounds.max[2]);
                have_model_bounds_max = ok;
                if(!ok)
                    break;
                continue;
            }

            ok = false;
            break;
        }

        if(scope == scope_vertex_layout)
        {
            if(token_count != 2)
            {
                ok = false;
                break;
            }
            if(strcmp(tokens[0], "position") == 0 && strcmp(tokens[1], "f32x3") == 0)
                have_layout_position = true;
            else if(strcmp(tokens[0], "normal") == 0 && strcmp(tokens[1], "f32x3") == 0)
                have_layout_normal = true;
            else if(strcmp(tokens[0], "uv0") == 0 && strcmp(tokens[1], "f32x2") == 0)
                have_layout_uv0 = true;
            else if(strcmp(tokens[0], "tangent") == 0 && strcmp(tokens[1], "f32x4") == 0)
                have_layout_tangent = true;
            else if(strcmp(tokens[0], "joints0") == 0 && strcmp(tokens[1], "u16x4") == 0)
                have_layout_joints = true;
            else if(strcmp(tokens[0], "weights0") == 0 && strcmp(tokens[1], "unorm16x4") == 0)
                have_layout_weights = true;
            else
                ok = false;
            if(!ok)
                break;
            continue;
        }

        if(scope == scope_material)
        {
            if(current_material == UINT32_MAX)
            {
                ok = false;
                break;
            }

            ModelMaterialSource* mat = &src.materials[current_material];

            if(strcmp(tokens[0], "name") == 0 && token_count == 2)
            {
                current_material_parse.seen_name = true;
                continue;
            }

            if(strcmp(tokens[0], "base_color_tex") == 0 && token_count == 2)
            {
                free(mat->base_color_tex);
                mat->base_color_tex = strdup_owned(tokens[1]);
                current_material_parse.seen_base_color_tex = (mat->base_color_tex != NULL);
                ok = current_material_parse.seen_base_color_tex;
                if(!ok)
                    break;
                continue;
            }

            if(strcmp(tokens[0], "normal_tex") == 0 && token_count == 2)
            {
                free(mat->normal_tex);
                mat->normal_tex = strdup_owned(tokens[1]);
                current_material_parse.seen_normal_tex = (mat->normal_tex != NULL);
                ok = current_material_parse.seen_normal_tex;
                if(!ok)
                    break;
                continue;
            }

            if(strcmp(tokens[0], "orm_tex") == 0 && token_count == 2)
            {
                free(mat->orm_tex);
                mat->orm_tex = strdup_owned(tokens[1]);
                current_material_parse.seen_orm_tex = (mat->orm_tex != NULL);
                ok = current_material_parse.seen_orm_tex;
                if(!ok)
                    break;
                continue;
            }

            if(strcmp(tokens[0], "base_color_factor") == 0 && token_count == 5)
            {
                ok = helpers_parse_f32(tokens[1], &mat->base_color_factor[0])
                     && helpers_parse_f32(tokens[2], &mat->base_color_factor[1])
                     && helpers_parse_f32(tokens[3], &mat->base_color_factor[2])
                     && helpers_parse_f32(tokens[4], &mat->base_color_factor[3]);
                current_material_parse.seen_base_color_factor = ok;
                if(!ok)
                    break;
                continue;
            }

            if(strcmp(tokens[0], "metallic") == 0 && token_count == 2)
            {
                ok = helpers_parse_f32(tokens[1], &mat->metallic_factor);
                current_material_parse.seen_metallic = ok;
                if(!ok)
                    break;
                continue;
            }

            if(strcmp(tokens[0], "roughness") == 0 && token_count == 2)
            {
                ok = helpers_parse_f32(tokens[1], &mat->roughness_factor);
                current_material_parse.seen_roughness = ok;
                if(!ok)
                    break;
                continue;
            }

            if(strcmp(tokens[0], "alpha_mode") == 0 && token_count == 2)
            {
                if(strcmp(tokens[1], "opaque") == 0)
                    mat->flags &= ~1u;
                else if(strcmp(tokens[1], "mask") == 0)
                    mat->flags |= 1u;
                else if(strcmp(tokens[1], "blend") == 0)
                    mat->flags |= 2u;
                else
                {
                    ok = false;
                    break;
                }
                current_material_parse.seen_alpha_mode = true;
                continue;
            }

            ok = false;
            break;
        }

        if(scope == scope_submesh)
        {
            if(current_submesh == UINT32_MAX)
            {
                ok = false;
                break;
            }

            ModelSubmeshSource* sm = &src.submeshes[current_submesh];

            if(strcmp(tokens[0], "bounds") == 0)
            {
                pending_scope = scope_submesh_bounds;
                continue;
            }

            if(strcmp(tokens[0], "material") == 0 && token_count == 2)
            {
                ok = helpers_parse_u32(tokens[1], &sm->material_index);
                current_submesh_parse.seen_material = ok;
                if(!ok)
                    break;
                continue;
            }

            if(strcmp(tokens[0], "vertex_offset") == 0 && token_count == 2)
            {
                ok = helpers_parse_u32(tokens[1], &sm->vertex_offset);
                current_submesh_parse.seen_vertex_offset = ok;
                if(!ok)
                    break;
                continue;
            }

            if(strcmp(tokens[0], "vertex_count") == 0 && token_count == 2)
            {
                ok = helpers_parse_u32(tokens[1], &sm->vertex_count);
                current_submesh_parse.seen_vertex_count = ok;
                if(!ok)
                    break;
                continue;
            }

            if(strcmp(tokens[0], "index_offset") == 0 && token_count == 2)
            {
                ok = helpers_parse_u32(tokens[1], &sm->index_offset);
                current_submesh_parse.seen_index_offset = ok;
                if(!ok)
                    break;
                continue;
            }

            if(strcmp(tokens[0], "index_count") == 0 && token_count == 2)
            {
                ok = helpers_parse_u32(tokens[1], &sm->index_count);
                current_submesh_parse.seen_index_count = ok;
                if(!ok)
                    break;
                continue;
            }

            ok = false;
            break;
        }

        if(scope == scope_submesh_bounds)
        {
            if(current_submesh == UINT32_MAX || token_count != 4)
            {
                ok = false;
                break;
            }

            ModelSubmeshSource* sm = &src.submeshes[current_submesh];
            if(strcmp(tokens[0], "min") == 0)
            {
                ok = helpers_parse_f32(tokens[1], &sm->bounds.min[0])
                     && helpers_parse_f32(tokens[2], &sm->bounds.min[1])
                     && helpers_parse_f32(tokens[3], &sm->bounds.min[2]);
                current_submesh_parse.seen_bounds_min = ok;
                if(!ok)
                    break;
                continue;
            }

            if(strcmp(tokens[0], "max") == 0)
            {
                ok = helpers_parse_f32(tokens[1], &sm->bounds.max[0])
                     && helpers_parse_f32(tokens[2], &sm->bounds.max[1])
                     && helpers_parse_f32(tokens[3], &sm->bounds.max[2]);
                current_submesh_parse.seen_bounds_max = ok;
                if(!ok)
                    break;
                continue;
            }

            ok = false;
            break;
        }

        if(scope == scope_vertices)
        {
            if(strcmp(tokens[0], "v") != 0)
            {
                ok = false;
                break;
            }

            if(vertex_write_index == vertex_capacity)
            {
                uint32_t new_cap = vertex_capacity ? vertex_capacity * 2u : 256u;
                if(!meshx_reserve_vertices(&src, new_cap))
                {
                    ok = false;
                    break;
                }
                vertex_capacity = new_cap;
            }

            uint32_t ti = 1;
            float f0, f1, f2, f3;
            // position
            if(!helpers_parse_f32(tokens[ti++], &f0) || !helpers_parse_f32(tokens[ti++], &f1) || !helpers_parse_f32(tokens[ti++], &f2))
            {
                ok = false;
                break;
            }
            src.positions_xyz[vertex_write_index * 3u + 0u] = f0;
            src.positions_xyz[vertex_write_index * 3u + 1u] = f1;
            src.positions_xyz[vertex_write_index * 3u + 2u] = f2;

            // normal
            if(have_layout_normal)
            {
                if(!helpers_parse_f32(tokens[ti++], &f0) || !helpers_parse_f32(tokens[ti++], &f1) || !helpers_parse_f32(tokens[ti++], &f2))
                {
                    ok = false;
                    break;
                }
                src.normals_xyz[vertex_write_index * 3u + 0u] = f0;
                src.normals_xyz[vertex_write_index * 3u + 1u] = f1;
                src.normals_xyz[vertex_write_index * 3u + 2u] = f2;
            }

            // uv
            if(have_layout_uv0)
            {
                if(!helpers_parse_f32(tokens[ti++], &f0) || !helpers_parse_f32(tokens[ti++], &f1))
                {
                    ok = false;
                    break;
                }
                src.uv0_xy[vertex_write_index * 2u + 0u] = f0;
                src.uv0_xy[vertex_write_index * 2u + 1u] = f1;
            }

            // tangent
            if(have_layout_tangent)
            {
                if(!helpers_parse_f32(tokens[ti++], &f0) || !helpers_parse_f32(tokens[ti++], &f1) || !helpers_parse_f32(tokens[ti++], &f2) || !helpers_parse_f32(tokens[ti++], &f3))
                {
                    ok = false;
                    break;
                }
                src.tangents_xyzw[vertex_write_index * 4u + 0u] = f0;
                src.tangents_xyzw[vertex_write_index * 4u + 1u] = f1;
                src.tangents_xyzw[vertex_write_index * 4u + 2u] = f2;
                src.tangents_xyzw[vertex_write_index * 4u + 3u] = f3;
            }

            // joints
            if(have_layout_joints)
            {
                for(int j = 0; j < 4; ++j)
                {
                    uint32_t ui = 0;
                    if(!helpers_parse_u32(tokens[ti++], &ui))
                    {
                        ok = false;
                        break;
                    }
                    src.joints_u16[vertex_write_index * 4u + j] = (uint16_t)ui;
                }
                if(!ok)
                    break;
            }

            // weights (assume unorm16 provided as integer or float in 0..1)
            if(have_layout_weights)
            {
                for(int j = 0; j < 4; ++j)
                {
                    float wf = 0.0f;
                    if(helpers_parse_f32(tokens[ti], &wf))
                    {
                        src.weights_u16[vertex_write_index * 4u + j] = (uint16_t)(wf * 65535.0f);
                        ++ti;
                        continue;
                    }
                    uint32_t wi = 0;
                    if(!helpers_parse_u32(tokens[ti++], &wi))
                    {
                        ok = false;
                        break;
                    }
                    src.weights_u16[vertex_write_index * 4u + j] = (uint16_t)wi;
                }
                if(!ok)
                    break;
            }
            ++vertex_write_index;
            continue;
        }

        if(scope == scope_indices)
        {
            if(strcmp(tokens[0], "i") != 0 || token_count != 4)
            {
                ok = false;
                break;
            }

            if(index_write_index + 3u > index_capacity)
            {
                uint32_t new_cap = index_capacity ? index_capacity * 2u : 384u;
                while(index_write_index + 3u > new_cap)
                    new_cap *= 2u;
                if(!meshx_reserve_indices(&src, new_cap))
                {
                    ok = false;
                    break;
                }
                index_capacity = new_cap;
            }

            ok = helpers_parse_u32(tokens[1], &src.indices[index_write_index + 0u])
                 && helpers_parse_u32(tokens[2], &src.indices[index_write_index + 1u])
                 && helpers_parse_u32(tokens[3], &src.indices[index_write_index + 2u]);
            if(!ok)
                break;

            index_write_index += 3u;
            continue;
        }

        ok = false;
        break;
    }

    free(text);

    if(ok)
    {
        src.vertex_count = vertex_write_index;
        src.index_count = index_write_index;
    }

    if(ok && !(have_model_bounds_min && have_model_bounds_max && have_layout_position && have_layout_normal && have_layout_uv0 && have_layout_tangent))
        ok = false;

    if(ok && (src.vertex_count == 0 || src.index_count == 0 || src.submesh_count == 0 || src.material_count == 0))
        ok = false;

    if(ok)
    {
        src.positions_xyz = (float*)realloc(src.positions_xyz, (size_t)src.vertex_count * 3u * sizeof(float));
        src.uv0_xy = (float*)realloc(src.uv0_xy, (size_t)src.vertex_count * 2u * sizeof(float));
        src.indices = (uint32_t*)realloc(src.indices, (size_t)src.index_count * sizeof(uint32_t));
        if(!src.positions_xyz || !src.uv0_xy || !src.indices)
            ok = false;
    }

    for(uint32_t i = 0; ok && i < src.submesh_count; ++i)
    {
        const ModelSubmeshSource* sm = &src.submeshes[i];
        if(sm->material_index >= src.material_count)
            ok = false;
        if(sm->vertex_offset + sm->vertex_count > src.vertex_count)
            ok = false;
        if(sm->index_offset + sm->index_count > src.index_count)
            ok = false;
    }

    if(!ok)
    {
        model_source_free(&src);
        return false;
    }

    *out_src = src;
    return true;
}

static bool model_asset_remove_active_id(ModelAssetSystem* sys, uint32_t id)
{
    for(uint32_t i = 0; i < sys->active_model_count; ++i)
    {
        if(sys->active_model_ids[i] == id)
        {
            sys->active_model_ids[i] = sys->active_model_ids[sys->active_model_count - 1u];
            sys->active_model_count--;
            return true;
        }
    }
    return false;
}

void model_api_unload(ModelHandle model)
{
    if(!g_model_api.initialized)
        return;

    ModelAssetSystem* sys = &g_model_api.assets;
    if(!model_handle_valid(sys, model))
        return;

    ModelInstanceSystem* inst_sys = &g_model_api.instance_system;
    for(uint32_t i = 0; i < inst_sys->active_instance_count; )
    {
        uint32_t iid = inst_sys->active_instance_ids[i];
        if(iid < inst_sys->instance_capacity && inst_sys->instances[iid].active && inst_sys->instances[iid].model == model)
        {
            inst_sys->instances[iid].active = false;
            memset(&inst_sys->instances[iid], 0, sizeof(ModelInstanceData));
            mu_id_pool_destroy_id(&inst_sys->instance_id_pool, iid);
            inst_sys->active_instance_ids[i] = inst_sys->active_instance_ids[inst_sys->active_instance_count - 1u];
            inst_sys->active_instance_count--;
            continue;
        }
        ++i;
    }

    ModelData* m = &sys->models[model];

    for(uint32_t i = 0; i < m->submesh_count; ++i)
    {
        uint32_t sid = m->first_submesh_id + i;
        if(sid >= sys->submesh_count)
            break;
        if(sys->submesh_table[sid].position_slice.buffer != VK_NULL_HANDLE)
            buffer_pool_free(sys->submesh_table[sid].position_slice);
        if(sys->submesh_table[sid].uv_slice.buffer != VK_NULL_HANDLE)
            buffer_pool_free(sys->submesh_table[sid].uv_slice);
        if(sys->submesh_table[sid].index_slice.buffer != VK_NULL_HANDLE)
            buffer_pool_free(sys->submesh_table[sid].index_slice);

        memset(&sys->submesh_table[sid], 0, sizeof(Submesh));
        memset(&sys->submesh_meta_table[sid], 0, sizeof(SubmeshMetaGpu));
    }

    for(uint32_t i = 0; i < m->material_count; ++i)
    {
        uint32_t mid = m->first_material_id + i;
        if(mid >= sys->material_count)
            break;

        if(sys->material_table[mid].base_color_texture_id != MODEL_INVALID_TEXTURE_ID)
            destroy_texture(&renderer, sys->material_table[mid].base_color_texture_id);
        if(sys->material_table[mid].normal_texture_id != MODEL_INVALID_TEXTURE_ID)
            destroy_texture(&renderer, sys->material_table[mid].normal_texture_id);
        if(sys->material_table[mid].orm_texture_id != MODEL_INVALID_TEXTURE_ID)
            destroy_texture(&renderer, sys->material_table[mid].orm_texture_id);

        memset(&sys->material_table[mid], 0, sizeof(MaterialData));
        memset(&sys->material_gpu_table[mid], 0, sizeof(MaterialGpu));
    }

    for(uint32_t i = 0; i < m->clip_count; ++i)
    {
        uint32_t cid = m->first_clip_id + i;
        if(cid >= sys->clip_count)
            break;
        free(sys->clip_table[cid].name);
        memset(&sys->clip_table[cid], 0, sizeof(AnimationClipData));
    }

    free(sys->debug_paths[model]);
    sys->debug_paths[model] = NULL;

    memset(m, 0, sizeof(*m));

    mu_id_pool_destroy_id(&sys->model_id_pool, model);
    if(sys->model_count > 0)
        sys->model_count--;
    model_asset_remove_active_id(sys, model);

    sys->submesh_meta_dirty = true;
    sys->material_dirty = true;
}

static bool model_asset_create_from_source(ModelAssetSystem* sys, const char* source_path, ModelSource* src, ModelHandle* out_model)
{
    if(!sys || !src || !out_model || !source_path)
        return false;

    if(src->submesh_count == 0 || src->material_count == 0)
        return false;

    if(sys->submesh_count + src->submesh_count > sys->submesh_capacity)
        return false;
    if(sys->material_count + src->material_count > sys->material_capacity)
        return false;
    if(sys->clip_count + src->clip_count > sys->clip_capacity)
        return false;

    uint32_t id = MODEL_HANDLE_INVALID;
    if(!mu_id_pool_create_id(&sys->model_id_pool, &id))
        return false;

    if(sys->active_model_count >= sys->model_capacity)
    {
        mu_id_pool_destroy_id(&sys->model_id_pool, id);
        return false;
    }

    uint32_t first_submesh = sys->submesh_count;
    uint32_t first_material = sys->material_count;
    uint32_t first_clip = sys->clip_count;

    char* path_copy = strdup_owned(source_path);
    if(!path_copy)
    {
        mu_id_pool_destroy_id(&sys->model_id_pool, id);
        return false;
    }

    for(uint32_t i = 0; i < src->material_count; ++i)
    {
        uint32_t mid = first_material + i;
        MaterialData* out_mat = &sys->material_table[mid];
        MaterialGpu* out_gpu = &sys->material_gpu_table[mid];

        out_mat->base_color_texture_id = MODEL_INVALID_TEXTURE_ID;
        out_mat->normal_texture_id = MODEL_INVALID_TEXTURE_ID;
        out_mat->orm_texture_id = MODEL_INVALID_TEXTURE_ID;
        out_mat->sampler_id = renderer.default_samplers.samplers[SAMPLER_LINEAR_WRAP];
        out_mat->base_color_factor[0] = src->materials[i].base_color_factor[0];
        out_mat->base_color_factor[1] = src->materials[i].base_color_factor[1];
        out_mat->base_color_factor[2] = src->materials[i].base_color_factor[2];
        out_mat->base_color_factor[3] = src->materials[i].base_color_factor[3];
        out_mat->metallic_factor = src->materials[i].metallic_factor;
        out_mat->roughness_factor = src->materials[i].roughness_factor;
        out_mat->flags = src->materials[i].flags;

        char full_path[PATH_MAX];
        if(src->materials[i].base_color_tex && resolve_texture_path(source_path, src->materials[i].base_color_tex, full_path, sizeof(full_path)))
        {
            TextureID tex = load_texture(&renderer, full_path);
            if(tex != MODEL_INVALID_TEXTURE_ID)
                out_mat->base_color_texture_id = tex;
        }
        if(src->materials[i].normal_tex && resolve_texture_path(source_path, src->materials[i].normal_tex, full_path, sizeof(full_path)))
        {
            TextureID tex = load_texture(&renderer, full_path);
            if(tex != MODEL_INVALID_TEXTURE_ID)
                out_mat->normal_texture_id = tex;
        }
        if(src->materials[i].orm_tex && resolve_texture_path(source_path, src->materials[i].orm_tex, full_path, sizeof(full_path)))
        {
            TextureID tex = load_texture(&renderer, full_path);
            if(tex != MODEL_INVALID_TEXTURE_ID)
                out_mat->orm_texture_id = tex;
        }

        out_gpu->base_color_texture_id = out_mat->base_color_texture_id;
        out_gpu->sampler_id = out_mat->sampler_id;
        out_gpu->base_color_factor[0] = out_mat->base_color_factor[0];
        out_gpu->base_color_factor[1] = out_mat->base_color_factor[1];
        out_gpu->base_color_factor[2] = out_mat->base_color_factor[2];
        out_gpu->base_color_factor[3] = out_mat->base_color_factor[3];
    }

    for(uint32_t i = 0; i < src->clip_count; ++i)
    {
        uint32_t cid = first_clip + i;
        sys->clip_table[cid].duration = src->clips[i].duration;
        sys->clip_table[cid].name = src->clips[i].name ? strdup_owned(src->clips[i].name) : NULL;
        if(src->clips[i].name && !sys->clip_table[cid].name)
        {
            free(path_copy);
            mu_id_pool_destroy_id(&sys->model_id_pool, id);
            return false;
        }
    }

    // Compute normals and tangents if needed for skinning
    if(src->has_skinning)
    {
        if(!src->has_normal && !compute_normals(src))
        {
            free(path_copy);
            mu_id_pool_destroy_id(&sys->model_id_pool, id);
            return false;
        }
        if(!src->has_tangent && !compute_tangents(src))
        {
            free(path_copy);
            mu_id_pool_destroy_id(&sys->model_id_pool, id);
            return false;
        }
    }

    for(uint32_t i = 0; i < src->submesh_count; ++i)
    {
        uint32_t sid = first_submesh + i;
        const ModelSubmeshSource* in_sm = &src->submeshes[i];
        Submesh* out_sm = &sys->submesh_table[sid];

        VkDeviceSize pos_bytes = (VkDeviceSize)in_sm->vertex_count * 3u * sizeof(float);
        VkDeviceSize uv_bytes = (VkDeviceSize)in_sm->vertex_count * 2u * sizeof(float);
        VkDeviceSize idx_bytes = (VkDeviceSize)in_sm->index_count * sizeof(uint32_t);
        VkDeviceSize packed_bytes = src->has_skinning ? (VkDeviceSize)in_sm->vertex_count * 56u : 0;  // 56 = 12+12+16+8+8

        out_sm->position_slice = buffer_pool_alloc(&renderer.gpu_pool, pos_bytes, 16);
        out_sm->uv_slice = buffer_pool_alloc(&renderer.gpu_pool, uv_bytes, 16);
        out_sm->index_slice = buffer_pool_alloc(&renderer.gpu_pool, idx_bytes, 16);
        if(packed_bytes > 0)
            out_sm->packed_slice = buffer_pool_alloc(&renderer.gpu_pool, packed_bytes, 16);

        if(out_sm->position_slice.buffer == VK_NULL_HANDLE || out_sm->uv_slice.buffer == VK_NULL_HANDLE || out_sm->index_slice.buffer == VK_NULL_HANDLE
           || (packed_bytes > 0 && out_sm->packed_slice.buffer == VK_NULL_HANDLE))
        {
            if(out_sm->position_slice.buffer != VK_NULL_HANDLE)
                buffer_pool_free(out_sm->position_slice);
            if(out_sm->uv_slice.buffer != VK_NULL_HANDLE)
                buffer_pool_free(out_sm->uv_slice);
            if(out_sm->index_slice.buffer != VK_NULL_HANDLE)
                buffer_pool_free(out_sm->index_slice);
            if(out_sm->packed_slice.buffer != VK_NULL_HANDLE)
                buffer_pool_free(out_sm->packed_slice);
            free(path_copy);
            mu_id_pool_destroy_id(&sys->model_id_pool, id);
            return false;
        }

        const float* pos_src = src->positions_xyz + (size_t)in_sm->vertex_offset * 3u;
        const float* uv_src = src->uv0_xy + (size_t)in_sm->vertex_offset * 2u;
        const uint32_t* idx_src = src->indices + in_sm->index_offset;

        if(!model_asset_push_upload(sys, out_sm->position_slice, pos_src, (uint32_t)pos_bytes, 16, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT)
           || !model_asset_push_upload(sys, out_sm->uv_slice, uv_src, (uint32_t)uv_bytes, 16, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT)
           || !model_asset_push_upload(sys, out_sm->index_slice, idx_src, (uint32_t)idx_bytes, 16, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT))
        {
            free(path_copy);
            mu_id_pool_destroy_id(&sys->model_id_pool, id);
            return false;
        }

        // Pack and upload skin vertices if needed
        if(packed_bytes > 0)
        {
            uint8_t* packed_data = (uint8_t*)malloc((size_t)packed_bytes);
            if(!packed_data)
            {
                free(path_copy);
                mu_id_pool_destroy_id(&sys->model_id_pool, id);
                return false;
            }
            if(!pack_skin_vertices(src, in_sm->vertex_offset, in_sm->vertex_count, packed_data))
            {
                free(packed_data);
                free(path_copy);
                mu_id_pool_destroy_id(&sys->model_id_pool, id);
                return false;
            }
            if(!model_asset_push_upload(sys, out_sm->packed_slice, packed_data, (uint32_t)packed_bytes, 16, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT))
            {
                free(packed_data);
                free(path_copy);
                mu_id_pool_destroy_id(&sys->model_id_pool, id);
                return false;
            }
            free(packed_data);
        }

        out_sm->vertex_offset = 0;
        out_sm->index_offset = 0;
        out_sm->vertex_count = in_sm->vertex_count;
        out_sm->index_count = in_sm->index_count;
        out_sm->material_id = first_material + in_sm->material_index;
        out_sm->flags = in_sm->flags;
        out_sm->bounds = in_sm->bounds;

        sys->submesh_meta_table[sid].pos_ptr = slice_device_address(&renderer, out_sm->position_slice);
        sys->submesh_meta_table[sid].uv_ptr = slice_device_address(&renderer, out_sm->uv_slice);
        sys->submesh_meta_table[sid].idx_ptr = slice_device_address(&renderer, out_sm->index_slice);
        sys->submesh_meta_table[sid].index_count = out_sm->index_count;
        sys->submesh_meta_table[sid].material_id = out_sm->material_id;
    }

    sys->models[id].first_submesh_id = first_submesh;
    sys->models[id].submesh_count = src->submesh_count;
    sys->models[id].first_material_id = first_material;
    sys->models[id].material_count = src->material_count;
    sys->models[id].first_clip_id = first_clip;
    sys->models[id].clip_count = src->clip_count;
    sys->models[id].has_skinning = src->has_skinning ? 1u : 0u;
    sys->models[id].has_skeleton = src->has_skeleton ? 1u : 0u;
    sys->models[id].bounds = src->bounds;

    sys->debug_paths[id] = path_copy;
    sys->active_model_ids[sys->active_model_count++] = id;

    sys->submesh_count += src->submesh_count;
    sys->material_count += src->material_count;
    sys->clip_count += src->clip_count;
    sys->model_count++;

    sys->submesh_meta_dirty = true;
    sys->material_dirty = true;

    *out_model = id;
    return true;
}

static bool model_asset_upload_pending(ModelAssetSystem* sys, VkCommandBuffer cmd)
{
    if(!sys)     
        return false;

    if(sys->pending_upload_count > 0)
    {
        VkBufferMemoryBarrier2* barriers = (VkBufferMemoryBarrier2*)calloc(sys->pending_upload_count, sizeof(VkBufferMemoryBarrier2));
        if(!barriers)
            return false;

        uint32_t written = 0;
        uint32_t read = 0;
        uint32_t barrier_count = 0;

        for(; read < sys->pending_upload_count; ++read)
        {
            UploadRequest* req = &sys->pending_uploads[read];
            if(!renderer_upload_buffer_to_slice(&renderer, cmd, req->dst, req->src, req->size, req->alignment))
            {
                sys->pending_uploads[written++] = *req;
                continue;
            }

            barriers[barrier_count++] = (VkBufferMemoryBarrier2){
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = req->stage_flags,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                .buffer = req->dst.buffer,
                .offset = req->dst.offset,
                .size = req->size,
            };

            free(req->src);
        }

        if(barrier_count > 0)
        {
            VkDependencyInfo dep = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .bufferMemoryBarrierCount = barrier_count,
                .pBufferMemoryBarriers = barriers,
            };
            vkCmdPipelineBarrier2(cmd, &dep);
        }

        free(barriers);
        sys->pending_upload_count = written;
    }

    if(sys->submesh_meta_dirty && sys->submesh_count > 0)
    {
        VkDeviceSize bytes = (VkDeviceSize)sys->submesh_count * sizeof(SubmeshMetaGpu);
        if(renderer_upload_buffer_to_slice(&renderer, cmd, sys->submesh_meta_slice, sys->submesh_meta_table, bytes, 16))
        {
            VkBufferMemoryBarrier2 barrier = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                .buffer = sys->submesh_meta_slice.buffer,
                .offset = sys->submesh_meta_slice.offset,
                .size = bytes,
            };
            VkDependencyInfo dep = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .bufferMemoryBarrierCount = 1,
                .pBufferMemoryBarriers = &barrier,
            };
            vkCmdPipelineBarrier2(cmd, &dep);
            sys->submesh_meta_dirty = false;
        }
    }

    if(sys->material_dirty && sys->material_count > 0)
    {
        VkDeviceSize bytes = (VkDeviceSize)sys->material_count * sizeof(MaterialGpu);
        if(renderer_upload_buffer_to_slice(&renderer, cmd, sys->material_gpu_slice, sys->material_gpu_table, bytes, 16))
        {
            VkBufferMemoryBarrier2 barrier = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                .buffer = sys->material_gpu_slice.buffer,
                .offset = sys->material_gpu_slice.offset,
                .size = bytes,
            };
            VkDependencyInfo dep = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .bufferMemoryBarrierCount = 1,
                .pBufferMemoryBarriers = &barrier,
            };
            vkCmdPipelineBarrier2(cmd, &dep);
            sys->material_dirty = false;
        }
    }

    return true;
}

bool model_api_init(uint32_t max_models, uint32_t instance_capacity)
{
    if(g_model_api.initialized || max_models == 0 || instance_capacity == 0)
        return false;

    memset(&g_model_api, 0, sizeof(g_model_api));
    
    // Capture global renderer for compute operations
    g_model_api.renderer = &renderer;

    // Initialize new modular systems
    if(!model_instances_init(instance_capacity))
        goto error_exit;

    if(!animation_system_init())
        goto error_exit;

    if(!model_render_init(instance_capacity))
        goto error_exit;

    if(!model_assets_init(max_models))
        goto error_exit;

    // Initialize legacy internal systems (will be replaced by new modules)
    if(!model_asset_system_init(&g_model_api.assets, max_models))
        goto error_exit;

    if(!model_queue_init(&g_model_api.queue, instance_capacity))
        goto error_exit;

    if(!model_instance_system_init(&g_model_api.instance_system, instance_capacity))
        goto error_exit;
    
    // Create compute skinning pipeline from compiled SPIR-V.
    g_model_api.skinning_pipeline = pipeline_create_compute(&renderer, "compiledshaders/skinning.comp.spv");

    g_model_api.debug_mode = ANIM_DEBUG_OFF;
    g_model_api.initialized = true;
    return true;

error_exit:
    // Shutdown in reverse order of initialization
    model_assets_shutdown();
    model_render_shutdown();
    animation_system_shutdown();
    model_instances_shutdown();
    
    // Shutdown legacy systems if they were partially initialized
    if(g_model_api.instance_system.active_instance_ids)
        model_instance_system_shutdown(&g_model_api.instance_system);
    if(g_model_api.queue.requests)
        model_queue_shutdown(&g_model_api.queue);
    if(g_model_api.assets.models)
        model_asset_system_shutdown(&g_model_api.assets);

    memset(&g_model_api, 0, sizeof(g_model_api));
    return false;
}

void model_api_shutdown(void)
{
    if(!g_model_api.initialized)
        return;

    // Shutdown new modular systems first (reverse order)
    model_assets_shutdown();
    model_render_shutdown();
    animation_system_shutdown();
    model_instances_shutdown();

    // Shutdown legacy systems
    model_instance_system_shutdown(&g_model_api.instance_system);
    model_queue_shutdown(&g_model_api.queue);
    model_asset_system_shutdown(&g_model_api.assets);
    
    memset(&g_model_api, 0, sizeof(g_model_api));
}

bool model_api_load_meshx(const char* path, ModelHandle* out_model)
{
    if(!g_model_api.initialized || !path || !out_model)
        return false;

    ModelSource src = {0};
    if(!model_source_from_meshx(path, &src))
        return false;

    bool ok = model_asset_create_from_source(&g_model_api.assets, path, &src, out_model);
    model_source_free(&src);
    return ok;
}

bool model_api_find_or_load_meshx(const char* path, ModelHandle* out_model)
{
    if(!g_model_api.initialized || !path || !out_model)
        return false;

    ModelHandle existing = model_asset_find_by_path(&g_model_api.assets, path);
    if(existing != MODEL_HANDLE_INVALID)
    {
        *out_model = existing;
        return true;
    }

    return model_api_load_meshx(path, out_model);
}

bool model_api_is_valid(ModelHandle model)
{
    if(!g_model_api.initialized)
        return false;
    return model_handle_valid(&g_model_api.assets, model);
}

bool model_api_has_skeleton(ModelHandle model)
{
    if(!model_api_is_valid(model))
        return false;
    return g_model_api.assets.models[model].has_skeleton != 0;
}

bool model_api_has_skinning(ModelHandle model)
{
    if(!model_api_is_valid(model))
        return false;
    return g_model_api.assets.models[model].has_skinning != 0;
}

bool model_api_has_animations(ModelHandle model)
{
    if(!model_api_is_valid(model))
        return false;
    return g_model_api.assets.models[model].clip_count > 0;
}

uint32_t model_api_animation_count(ModelHandle model)
{
    if(!model_api_is_valid(model))
        return 0;
    return g_model_api.assets.models[model].clip_count;
}

AnimationClipHandle model_api_find_clip(ModelHandle model, const char* name)
{
    if(!model_api_is_valid(model) || !name)
        return ANIMATION_CLIP_INVALID;

    const ModelData* m = &g_model_api.assets.models[model];
    for(uint32_t i = 0; i < m->clip_count; ++i)
    {
        uint32_t cid = m->first_clip_id + i;
        if(cid >= g_model_api.assets.clip_count)
            break;
        const AnimationClipData* clip = &g_model_api.assets.clip_table[cid];
        if(clip->name && strcmp(clip->name, name) == 0)
            return i;
    }

    return ANIMATION_CLIP_INVALID;
}

const char* model_api_clip_name(ModelHandle model, AnimationClipHandle clip)
{
    if(!model_api_is_valid(model))
        return NULL;

    const ModelData* m = &g_model_api.assets.models[model];
    if(clip >= m->clip_count)
        return NULL;

    uint32_t cid = m->first_clip_id + clip;
    if(cid >= g_model_api.assets.clip_count)
        return NULL;

    return g_model_api.assets.clip_table[cid].name;
}

float model_api_clip_duration(ModelHandle model, AnimationClipHandle clip)
{
    if(!model_api_is_valid(model))
        return 0.0f;

    const ModelData* m = &g_model_api.assets.models[model];
    if(clip >= m->clip_count)
        return 0.0f;

    uint32_t cid = m->first_clip_id + clip;
    if(cid >= g_model_api.assets.clip_count)
        return 0.0f;

    return g_model_api.assets.clip_table[cid].duration;
}

ModelInstanceHandle model_instance_create(ModelHandle model)
{
    if(!g_model_api.initialized || !model_api_is_valid(model))
        return MODEL_INSTANCE_HANDLE_INVALID;

    ModelInstanceSystem* sys = &g_model_api.instance_system;
    if(sys->active_instance_count >= sys->instance_capacity)
        return MODEL_INSTANCE_HANDLE_INVALID;

    uint32_t id = MODEL_INSTANCE_HANDLE_INVALID;
    if(!mu_id_pool_create_id(&sys->instance_id_pool, &id))
        return MODEL_INSTANCE_HANDLE_INVALID;

    ModelInstanceData* inst = &sys->instances[id];
    memset(inst, 0, sizeof(*inst));
    inst->model = model;
    mat4_identity(inst->transform);
    inst->color[0] = 1.0f;
    inst->color[1] = 1.0f;
    inst->color[2] = 1.0f;
    inst->color[3] = 1.0f;
    inst->anim = animation_state_default();
    inst->playback_mode = ANIM_LOOP;
    inst->active = true;

    // Allocate skinned vertex buffers if model has skinning
    ModelData* model_data = &g_model_api.assets.models[model];
    if(model_data->has_skinning)
    {
        // Calculate total vertex count from all submeshes
        uint32_t total_vertex_count = 0;
        for(uint32_t i = 0; i < model_data->submesh_count; ++i)
        {
            Submesh* sm = &g_model_api.assets.submesh_table[model_data->first_submesh_id + i];
            total_vertex_count += sm->vertex_count;
        }

        // Allocate skinned output buffer (3 floats position + 3 floats normal + 3 floats tangent per vertex)
        uint32_t skinned_vertex_size = total_vertex_count * (3 * 4 + 3 * 4 + 3 * 4);
        inst->skinned_vertex_buffer = buffer_pool_alloc(&g_model_api.renderer->gpu_pool, skinned_vertex_size, 16);

        // Allocate palette buffer (256 mat4s max)
        uint32_t palette_size = 256 * sizeof(float) * 16;
        inst->palette_buffer = buffer_pool_alloc(&g_model_api.renderer->gpu_pool, palette_size, 16);
        inst->palette_dirty = true;
    }

    sys->active_instance_ids[sys->active_instance_count++] = id;
    return id;
}

void model_instance_destroy(ModelInstanceHandle instance)
{
    if(!model_instance_valid(&g_model_api, instance))
        return;

    ModelInstanceSystem* sys = &g_model_api.instance_system;
    sys->instances[instance].active = false;
    memset(&sys->instances[instance], 0, sizeof(ModelInstanceData));
    mu_id_pool_destroy_id(&sys->instance_id_pool, instance);
    model_instance_remove_active_id(sys, instance);
}

bool model_instance_is_valid(ModelInstanceHandle instance)
{
    return model_instance_valid(&g_model_api, instance);
}

ModelHandle model_instance_model(ModelInstanceHandle instance)
{
    if(!model_instance_valid(&g_model_api, instance))
        return MODEL_HANDLE_INVALID;
    return g_model_api.instance_system.instances[instance].model;
}

void model_instance_set_transform(ModelInstanceHandle instance, const float model_matrix[4][4])
{
    if(!model_instance_valid(&g_model_api, instance) || !model_matrix)
        return;
    memcpy(g_model_api.instance_system.instances[instance].transform, model_matrix, sizeof(float[4][4]));
}

void model_instance_get_transform(ModelInstanceHandle instance, float out_model_matrix[4][4])
{
    if(!model_instance_valid(&g_model_api, instance) || !out_model_matrix)
        return;
    memcpy(out_model_matrix, g_model_api.instance_system.instances[instance].transform, sizeof(float[4][4]));
}

void model_instance_set_color(ModelInstanceHandle instance, const float color[4])
{
    if(!model_instance_valid(&g_model_api, instance) || !color)
        return;
    memcpy(g_model_api.instance_system.instances[instance].color, color, sizeof(float[4]));
}

bool model_instance_play(ModelInstanceHandle instance, AnimationClipHandle clip, AnimationPlaybackMode mode)
{
    if(!model_instance_valid(&g_model_api, instance))
        return false;

    ModelInstanceData* inst = &g_model_api.instance_system.instances[instance];
    uint32_t clip_count = model_api_animation_count(inst->model);
    if(clip_count == 0 || clip >= clip_count)
        return false;

    inst->anim.clip = clip;
    inst->anim.time = 0.0f;
    inst->anim.playing = true;
    inst->anim.paused = false;
    inst->playback_mode = mode;
    inst->anim.loop = (mode != ANIM_PLAY_ONCE);
    return true;
}

bool model_instance_play_by_name(ModelInstanceHandle instance, const char* clip_name, AnimationPlaybackMode mode)
{
    if(!model_instance_valid(&g_model_api, instance) || !clip_name)
        return false;

    ModelHandle model = g_model_api.instance_system.instances[instance].model;
    AnimationClipHandle clip = model_api_find_clip(model, clip_name);
    if(clip == ANIMATION_CLIP_INVALID)
        return false;

    return model_instance_play(instance, clip, mode);
}

void model_instance_stop(ModelInstanceHandle instance)
{
    if(!model_instance_valid(&g_model_api, instance))
        return;
    ModelInstanceData* inst = &g_model_api.instance_system.instances[instance];
    inst->anim.playing = false;
    inst->anim.time = 0.0f;
}

void model_instance_pause(ModelInstanceHandle instance, bool paused)
{
    if(!model_instance_valid(&g_model_api, instance))
        return;
    g_model_api.instance_system.instances[instance].anim.paused = paused;
}

void model_instance_set_time(ModelInstanceHandle instance, float time_sec)
{
    if(!model_instance_valid(&g_model_api, instance))
        return;
    g_model_api.instance_system.instances[instance].anim.time = time_sec < 0.0f ? 0.0f : time_sec;
}

void model_instance_set_speed(ModelInstanceHandle instance, float speed)
{
    if(!model_instance_valid(&g_model_api, instance))
        return;
    g_model_api.instance_system.instances[instance].anim.speed = speed;
}

void model_instance_set_loop(ModelInstanceHandle instance, bool enabled)
{
    if(!model_instance_valid(&g_model_api, instance))
        return;
    g_model_api.instance_system.instances[instance].anim.loop = enabled;
    g_model_api.instance_system.instances[instance].playback_mode = enabled ? ANIM_LOOP : ANIM_PLAY_ONCE;
}

AnimationState model_instance_animation_state(ModelInstanceHandle instance)
{
    if(!model_instance_valid(&g_model_api, instance))
        return animation_state_default();
    return g_model_api.instance_system.instances[instance].anim;
}

bool model_instance_blend_to(ModelInstanceHandle instance, AnimationClipHandle target_clip, float blend_time_sec, AnimationPlaybackMode mode)
{
    (void)blend_time_sec;
    return model_instance_play(instance, target_clip, mode);
}

bool model_instance_blend_to_by_name(ModelInstanceHandle instance, const char* clip_name, float blend_time_sec, AnimationPlaybackMode mode)
{
    (void)blend_time_sec;
    return model_instance_play_by_name(instance, clip_name, mode);
}

void animation_system_begin_frame(float dt_sec)
{
    if(!g_model_api.initialized)
        return;
    
    // Store dt in legacy system for backward compatibility
    g_model_api.instance_system.frame_dt = dt_sec;

    // Call new modular animation system
    animation_system_update_frame(dt_sec);
}

void animation_system_update_instance(ModelInstanceHandle instance)
{
    if(!model_instance_valid(&g_model_api, instance))
        return;

    ModelInstanceData* inst = &g_model_api.instance_system.instances[instance];
    if(!inst->anim.playing || inst->anim.paused || inst->anim.clip == ANIMATION_CLIP_INVALID)
        return;

    float duration = model_api_clip_duration(inst->model, inst->anim.clip);
    if(duration <= 0.0f)
        return;

    inst->anim.time += g_model_api.instance_system.frame_dt * inst->anim.speed;
    if(inst->anim.loop)
    {
        while(inst->anim.time >= duration)
            inst->anim.time -= duration;
        while(inst->anim.time < 0.0f)
            inst->anim.time += duration;
    }
    else
    {
        if(inst->anim.time >= duration)
        {
            inst->anim.time = duration;
            inst->anim.playing = false;
        }
        if(inst->anim.time < 0.0f)
            inst->anim.time = 0.0f;
    }
}

void animation_system_prepare_frame(VkCommandBuffer cmd)
{
    if(!g_model_api.initialized || !cmd)
        return;

    Renderer* r = g_model_api.renderer;
    if(!r || !g_model_api.skinning_pipeline)
        return;

    ModelInstanceSystem* sys = &g_model_api.instance_system;
    
    // Process each active instance that has skinning enabled
    for(uint32_t i = 0; i < sys->active_instance_count; ++i)
    {
        uint32_t inst_id = sys->active_instance_ids[i];
        ModelInstanceData* inst = &sys->instances[inst_id];
        
        if(!inst->active || !inst->palette_dirty)
            continue;

        ModelData* model_data = &g_model_api.assets.models[inst->model];
        if(!model_data->has_skinning)
            continue;

        // Calculate total vertex count from all submeshes
        uint32_t total_vertex_count = 0;
        for (uint32_t s = 0; s < model_data->submesh_count; ++s)
        {
            Submesh* sm = &g_model_api.assets.submesh_table[model_data->first_submesh_id + s];
            total_vertex_count += sm->vertex_count;
        }

        if(total_vertex_count == 0)
            continue;

        // For now, fill palette with identity matrices to test the compute dispatch
        // TODO: Evaluate animation clips and build proper joint matrices
        float identity_matrices[256 * 16];
        for(uint32_t j = 0; j < 256; ++j)
        {
            float* m = &identity_matrices[j * 16];
            memset(m, 0, sizeof(float) * 16);
            m[0] = 1.0f;
            m[5] = 1.0f;
            m[10] = 1.0f;
            m[15] = 1.0f;
        }

        // Upload palette to GPU
        void* palette_data = buffer_slice_get_mapped(&inst->palette_buffer);
        if(palette_data)
            memcpy(palette_data, identity_matrices, 256 * 16 * sizeof(float));

        // Get source vertex buffer device address (first submesh's packed buffer for skinning)
        Submesh* first_sm = &g_model_api.assets.submesh_table[model_data->first_submesh_id];
        VkDeviceAddress src_vertex_addr = buffer_slice_device_address(&first_sm->packed_slice);
        VkDeviceAddress dst_vertex_addr = buffer_slice_device_address(&inst->skinned_vertex_buffer);
        VkDeviceAddress palette_addr = buffer_slice_device_address(&inst->palette_buffer);

        if(!src_vertex_addr || !dst_vertex_addr || !palette_addr)
            continue;

        // Build push constants
        SkinningPush push = {
            .src_vertex_ptr = src_vertex_addr,
            .dst_vertex_ptr = dst_vertex_addr,
            .palette_ptr = palette_addr,
            .vertex_count = total_vertex_count,
            .joint_count = 256,  // TODO: Get actual joint count
        };

        // Bind compute pipeline and dispatch
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_get(g_model_api.skinning_pipeline));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->bindless_system.pipeline_layout, 0, 1, &r->bindless_system.set, 0, NULL);
        vkCmdPushConstants(cmd, r->bindless_system.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SkinningPush), &push);

        // Calculate workgroup count (64 threads per workgroup)
        uint32_t workgroups_x = (total_vertex_count + 63) / 64;
        vkCmdDispatch(cmd, workgroups_x, 1, 1);

        // Add buffer barrier: compute write -> graphics read
        VkBufferMemoryBarrier2 barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
            .buffer = r->gpu_pool.buffer,
            .offset = inst->skinned_vertex_buffer.offset,
            .size = inst->skinned_vertex_buffer.size,
        };

        VkDependencyInfo dep = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &barrier,
        };
        vkCmdPipelineBarrier2(cmd, &dep);

        inst->palette_dirty = false;
    }
}


bool model_instance_draw(ModelInstanceHandle instance)
{
    if(!model_instance_valid(&g_model_api, instance))
        return false;

    ModelInstanceData* inst = &g_model_api.instance_system.instances[instance];
    return model_api_draw(inst->model, inst->transform, inst->color);
}

void animation_debug_set_mode(AnimationDebugMode mode)
{
    g_model_api.debug_mode = mode;
}

void animation_debug_draw(ModelInstanceHandle instance)
{
    (void)instance;
}

void model_api_begin_frame(const Camera* cam)
{
    if(!g_model_api.initialized || !cam)
        return;

    ModelRenderQueue* queue = &g_model_api.queue;
    memcpy(queue->frame_view_proj, cam->view_proj, sizeof(queue->frame_view_proj));

    queue->request_count = 0;
    queue->transform_count = 0;
    queue->color_count = 0;
    queue->sort_item_count = 0;
    queue->instance_count = 0;
    queue->indirect_count = 0;
    queue->prepared = false;
}

bool model_api_draw(ModelHandle model, const float model_matrix[4][4], const float color[4])
{
    if(!g_model_api.initialized || !model_matrix || !color)
        return false;

    ModelAssetSystem* assets = &g_model_api.assets;
    ModelRenderQueue* queue = &g_model_api.queue;

    if(!model_handle_valid(assets, model))
        return false;

    if(queue->request_count >= queue->request_capacity)
        return false;

    uint32_t idx = queue->request_count++;

    queue->requests[idx].model = model;
    queue->requests[idx].transform_index = idx;
    queue->requests[idx].color_index = idx;
    queue->requests[idx].flags = 0;

    memcpy(queue->transforms[idx], model_matrix, sizeof(float[4][4]));
    memcpy(queue->colors[idx], color, sizeof(float[4]));

    queue->transform_count = queue->request_count;
    queue->color_count = queue->request_count;
    return true;
}

bool draw3d(ModelHandle model, const float model_matrix[4][4], const float color[4])
{
    return model_api_draw(model, model_matrix, color);
}

bool draw_model(const char* path, const float model_matrix[4][4])
{
    static const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    ModelHandle model = MODEL_HANDLE_INVALID;
    if(!path)
        return false;

    const char* ext = strrchr(path, '.');
    if(!ext || strcmp(ext, ".meshx") != 0)
        return false;

    if(!model_api_find_or_load_meshx(path, &model))
        return false;

    return model_api_draw(model, model_matrix, white);
}

void model_api_prepare_frame(VkCommandBuffer cmd)
{
    if(!g_model_api.initialized)
        return;

    ModelAssetSystem* assets = &g_model_api.assets;
    ModelRenderQueue* queue = &g_model_api.queue;

    if(!model_asset_upload_pending(assets, cmd))
        return;

    if(queue->request_count == 0)
    {
        queue->prepared = true;
        return;
    }

    queue->sort_item_count = 0;

    for(uint32_t i = 0; i < queue->request_count; ++i)
    {
        const DrawRequest* req = &queue->requests[i];
        if(!model_handle_valid(assets, req->model))
            continue;

        const ModelData* model = &assets->models[req->model];
        for(uint32_t s = 0; s < model->submesh_count; ++s)
        {
            uint32_t sid = model->first_submesh_id + s;
            if(sid >= assets->submesh_count)
                continue;

            if(queue->sort_item_count >= queue->sort_item_capacity)
                continue;

            const Submesh* submesh = &assets->submesh_table[sid];
            uint64_t key = ((uint64_t)0u << 56)
                         | ((uint64_t)(submesh->material_id & 0xFFFFFFu) << 24)
                         | (uint64_t)(sid & 0xFFFFFFu);

            DrawSortItem* item = &queue->sort_items[queue->sort_item_count++];
            item->sort_key = key;
            item->request_index = i;
            item->submesh_id = sid;
        }
    }

    if(queue->sort_item_count == 0)
    {
        queue->prepared = true;
        return;
    }

    qsort(queue->sort_items, queue->sort_item_count, sizeof(DrawSortItem), draw_sort_item_cmp);

    queue->instance_count = 0;
    for(uint32_t i = 0; i < queue->sort_item_count; ++i)
    {
        const DrawSortItem* item = &queue->sort_items[i];
        if(item->request_index >= queue->request_count || item->submesh_id >= assets->submesh_count)
            continue;

        if(queue->instance_count >= queue->instance_capacity)
            break;

        const DrawRequest* req = &queue->requests[item->request_index];
        const Submesh* submesh = &assets->submesh_table[item->submesh_id];

        ModelInstanceGpu* dst = &queue->instance_data[queue->instance_count++];
        dst->model_id = req->model;
        dst->submesh_id = item->submesh_id;
        dst->material_id = submesh->material_id;
        dst->flags = req->flags;
        memcpy(dst->transform, queue->transforms[req->transform_index], sizeof(dst->transform));
        memcpy(dst->color, queue->colors[req->color_index], sizeof(dst->color));

        // If there is an active instance with skinned vertex buffer for this model, use its device address
        VkDeviceAddress override_pos = 0;
        ModelInstanceSystem* inst_sys = &g_model_api.instance_system;
        for(uint32_t ai = 0; ai < inst_sys->active_instance_count; ++ai)
        {
            uint32_t iid = inst_sys->active_instance_ids[ai];
            if(iid >= inst_sys->instance_capacity)
                continue;
            ModelInstanceData* mid = &inst_sys->instances[iid];
            if(!mid->active)
                continue;
            if(mid->model != req->model)
                continue;
            if(mid->skinned_vertex_buffer.buffer == VK_NULL_HANDLE)
                continue;
            override_pos = buffer_slice_device_address(&mid->skinned_vertex_buffer);
            break;
        }
        dst->pos_ptr = override_pos;
    }

    queue->indirect_count = 0;
    uint32_t run_start = 0;
    while(run_start < queue->instance_count)
    {
        uint32_t sid = queue->instance_data[run_start].submesh_id;
        uint32_t mid = queue->instance_data[run_start].material_id;
        uint32_t run_end = run_start + 1u;

        while(run_end < queue->instance_count
              && queue->instance_data[run_end].submesh_id == sid
              && queue->instance_data[run_end].material_id == mid)
        {
            ++run_end;
        }

        if(queue->indirect_count >= queue->indirect_capacity)
            break;

        const Submesh* submesh = &assets->submesh_table[sid];

        VkDrawIndirectCommand* cmd_out = &queue->indirect_commands[queue->indirect_count++];
        cmd_out->vertexCount = submesh->index_count;
        cmd_out->instanceCount = run_end - run_start;
        cmd_out->firstVertex = 0;
        cmd_out->firstInstance = run_start;

        run_start = run_end;
    }

    if(queue->instance_count == 0 || queue->indirect_count == 0)
    {
        queue->prepared = true;
        return;
    }

    VkDeviceSize instance_bytes = (VkDeviceSize)queue->instance_count * sizeof(ModelInstanceGpu);
    VkDeviceSize indirect_bytes = (VkDeviceSize)queue->indirect_count * sizeof(VkDrawIndirectCommand);

    if(!renderer_upload_buffer_to_slice(&renderer, cmd, queue->instance_slice, queue->instance_data, instance_bytes, 16))
        return;

    if(!renderer_upload_buffer_to_slice(&renderer, cmd, queue->indirect_slice, queue->indirect_commands, indirect_bytes, 16))
        return;

    VkBufferMemoryBarrier2 barriers[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .buffer = queue->instance_slice.buffer,
            .offset = queue->instance_slice.offset,
            .size = instance_bytes,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
            .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
            .buffer = queue->indirect_slice.buffer,
            .offset = queue->indirect_slice.offset,
            .size = indirect_bytes,
        },
    };

    VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 2,
        .pBufferMemoryBarriers = barriers,
    };
    vkCmdPipelineBarrier2(cmd, &dep);

    queue->prepared = true;
}

void model_api_draw_queued(VkCommandBuffer cmd)
{
    if(!g_model_api.initialized)
        return;

    ModelAssetSystem* assets = &g_model_api.assets;
    ModelRenderQueue* queue = &g_model_api.queue;

    if(!queue->prepared || queue->indirect_count == 0 || queue->instance_count == 0)
        return;

    ModelIndirectPush push = {0};
    push.submesh_table_ptr = slice_device_address(&renderer, assets->submesh_meta_slice);
    push.material_table_ptr = slice_device_address(&renderer, assets->material_gpu_slice);
    push.instance_ptr = slice_device_address(&renderer, queue->instance_slice);
    memcpy(push.view_proj, queue->frame_view_proj, sizeof(push.view_proj));

    vk_cmd_set_viewport_scissor(cmd, renderer.swapchain.extent);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render_pipelines.pipelines[pipelines.gltf_minimal]);
    vkCmdPushConstants(cmd, renderer.bindless_system.pipeline_layout, VK_SHADER_STAGE_ALL, 0, sizeof(ModelIndirectPush), &push);
    vkCmdDrawIndirect(cmd, queue->indirect_slice.buffer, queue->indirect_slice.offset, queue->indirect_count, sizeof(VkDrawIndirectCommand));
}

void model_api_flush_frame(VkCommandBuffer cmd)
{
    model_api_prepare_frame(cmd);
    model_api_draw_queued(cmd);
}
