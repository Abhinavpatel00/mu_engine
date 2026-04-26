#include "gltf_gpu_mesh.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gltfloader_minimal.h"
#include "helpers.h"
#include "passes.h"

typedef struct AABB
{
    float min[3];
    float max[3];
} AABB;

typedef struct Submesh
{
    BufferSlice position_slice;
    BufferSlice uv_slice;
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
    AABB     bounds;
} ModelData;

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

typedef struct GltfIndirectPush
{
    VkDeviceAddress submesh_table_ptr;
    VkDeviceAddress material_table_ptr;
    VkDeviceAddress instance_ptr;
    uint64_t        pad0;
    float           view_proj[4][4];
} GltfIndirectPush;

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
    uint32_t* indices;
    uint32_t  vertex_count;
    uint32_t  index_count;

    ModelMaterialSource* materials;
    uint32_t             material_count;

    ModelSubmeshSource* submeshes;
    uint32_t            submesh_count;

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

static ModelApiState g_model_api = {0};

#define MODEL_INVALID_TEXTURE_ID UINT32_MAX
#define MESHX_MAX_TOKENS 24

static bool model_handle_valid(const ModelAssetSystem* sys, ModelHandle h)
{
    return h != MODEL_HANDLE_INVALID && h < sys->model_capacity && mu_id_pool_is_id(&sys->model_id_pool, h);
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

    if(!sys->models || !sys->debug_paths || !sys->active_model_ids || !sys->submesh_table || !sys->submesh_meta_table
       || !sys->material_table || !sys->material_gpu_table)
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

    for(uint32_t i = 0; i < sys->pending_upload_count; ++i)
        free(sys->pending_uploads[i].src);

    free(sys->pending_uploads);
    free(sys->material_gpu_table);
    free(sys->material_table);
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

static bool model_source_from_gltf(const char* path, ModelSource* out_src)
{
    GltfMinimalMesh mesh = {0};
    if(!gltf_minimal_load_first_mesh(path, &mesh))
        return false;

    ModelSource src = {0};

    src.vertex_count = mesh.vertex_count;
    src.index_count = mesh.index_count;
    src.positions_xyz = mesh.positions_xyz;
    src.uv0_xy = mesh.texcoord0_xy;
    src.indices = mesh.indices;

    src.material_count = 1;
    src.materials = (ModelMaterialSource*)calloc(1, sizeof(ModelMaterialSource));
    src.submesh_count = 1;
    src.submeshes = (ModelSubmeshSource*)calloc(1, sizeof(ModelSubmeshSource));

    if(!src.materials || !src.submeshes)
    {
        model_source_free(&src);
        gltf_minimal_free_mesh(&mesh);
        return false;
    }

    src.materials[0].base_color_tex = mesh.base_color_uri ? strdup_owned(mesh.base_color_uri) : NULL;
    src.materials[0].normal_tex = NULL;
    src.materials[0].orm_tex = NULL;
    src.materials[0].base_color_factor[0] = mesh.base_color_factor[0];
    src.materials[0].base_color_factor[1] = mesh.base_color_factor[1];
    src.materials[0].base_color_factor[2] = mesh.base_color_factor[2];
    src.materials[0].base_color_factor[3] = mesh.base_color_factor[3];
    src.materials[0].metallic_factor = 1.0f;
    src.materials[0].roughness_factor = 1.0f;

    src.submeshes[0].material_index = 0;
    src.submeshes[0].vertex_offset = 0;
    src.submeshes[0].vertex_count = src.vertex_count;
    src.submeshes[0].index_offset = 0;
    src.submeshes[0].index_count = src.index_count;
    aabb_make_default(&src.submeshes[0].bounds);
    aabb_make_default(&src.bounds);

    *out_src = src;
    return true;
}

static bool meshx_reserve_vertices(ModelSource* out_src, uint32_t vertex_count)
{
    if(vertex_count == 0)
        return false;

    out_src->positions_xyz = (float*)realloc(out_src->positions_xyz, (size_t)vertex_count * 3u * sizeof(float));
    out_src->uv0_xy = (float*)realloc(out_src->uv0_xy, (size_t)vertex_count * 2u * sizeof(float));
    return out_src->positions_xyz && out_src->uv0_xy;
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

    enum Scope scope = scope_root;
    enum Scope pending_scope = scope_root;
    uint32_t current_material = UINT32_MAX;
    uint32_t current_submesh = UINT32_MAX;

    uint32_t vertex_write_index = 0;
    uint32_t vertex_capacity = 0;
    uint32_t index_write_index = 0;
    uint32_t index_capacity = 0;

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
            if(pending_scope == scope_root)
            {
                ok = false;
                break;
            }
            scope = pending_scope;
            pending_scope = scope_root;
            continue;
        }

        if(strcmp(tokens[0], "}") == 0)
        {
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
            if(strcmp(tokens[0], "vertices") == 0)
            {
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
            if(strcmp(tokens[0], "v") != 0 || token_count != 13)
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

            float px, py, pz, u, v;
            ok = helpers_parse_f32(tokens[1], &px)
                 && helpers_parse_f32(tokens[2], &py)
                 && helpers_parse_f32(tokens[3], &pz)
                 && helpers_parse_f32(tokens[7], &u)
                 && helpers_parse_f32(tokens[8], &v);
            if(!ok)
                break;

            src.positions_xyz[vertex_write_index * 3u + 0u] = px;
            src.positions_xyz[vertex_write_index * 3u + 1u] = py;
            src.positions_xyz[vertex_write_index * 3u + 2u] = pz;
            src.uv0_xy[vertex_write_index * 2u + 0u] = u;
            src.uv0_xy[vertex_write_index * 2u + 1u] = v;
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

    for(uint32_t i = 0; i < src->submesh_count; ++i)
    {
        uint32_t sid = first_submesh + i;
        const ModelSubmeshSource* in_sm = &src->submeshes[i];
        Submesh* out_sm = &sys->submesh_table[sid];

        VkDeviceSize pos_bytes = (VkDeviceSize)in_sm->vertex_count * 3u * sizeof(float);
        VkDeviceSize uv_bytes = (VkDeviceSize)in_sm->vertex_count * 2u * sizeof(float);
        VkDeviceSize idx_bytes = (VkDeviceSize)in_sm->index_count * sizeof(uint32_t);

        out_sm->position_slice = buffer_pool_alloc(&renderer.gpu_pool, pos_bytes, 16);
        out_sm->uv_slice = buffer_pool_alloc(&renderer.gpu_pool, uv_bytes, 16);
        out_sm->index_slice = buffer_pool_alloc(&renderer.gpu_pool, idx_bytes, 16);
        if(out_sm->position_slice.buffer == VK_NULL_HANDLE || out_sm->uv_slice.buffer == VK_NULL_HANDLE || out_sm->index_slice.buffer == VK_NULL_HANDLE)
        {
            if(out_sm->position_slice.buffer != VK_NULL_HANDLE)
                buffer_pool_free(out_sm->position_slice);
            if(out_sm->uv_slice.buffer != VK_NULL_HANDLE)
                buffer_pool_free(out_sm->uv_slice);
            if(out_sm->index_slice.buffer != VK_NULL_HANDLE)
                buffer_pool_free(out_sm->index_slice);
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
    sys->models[id].bounds = src->bounds;

    sys->debug_paths[id] = path_copy;
    sys->active_model_ids[sys->active_model_count++] = id;

    sys->submesh_count += src->submesh_count;
    sys->material_count += src->material_count;
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

    if(!model_asset_system_init(&g_model_api.assets, max_models))
    {
        model_asset_system_shutdown(&g_model_api.assets);
        return false;
    }

    if(!model_queue_init(&g_model_api.queue, instance_capacity))
    {
        model_queue_shutdown(&g_model_api.queue);
        model_asset_system_shutdown(&g_model_api.assets);
        return false;
    }

    g_model_api.initialized = true;
    return true;
}

void model_api_shutdown(void)
{
    if(!g_model_api.initialized)
        return;

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

bool model_api_load_gltf(const char* path, ModelHandle* out_model)
{
    if(!g_model_api.initialized || !path || !out_model)
        return false;

    char meshx_path[PATH_MAX];
    if(path_try_meshx_sidecar(path, meshx_path, sizeof(meshx_path)))
        return model_api_load_meshx(meshx_path, out_model);

    ModelSource src = {0};
    if(!model_source_from_gltf(path, &src))
        return false;

    bool ok = model_asset_create_from_source(&g_model_api.assets, path, &src, out_model);
    model_source_free(&src);
    return ok;
}

bool model_api_find_or_load_gltf(const char* path, ModelHandle* out_model)
{
    if(!g_model_api.initialized || !path || !out_model)
        return false;

    ModelHandle existing = model_asset_find_by_path(&g_model_api.assets, path);
    if(existing != MODEL_HANDLE_INVALID)
    {
        *out_model = existing;
        return true;
    }

    return model_api_load_gltf(path, out_model);
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
    const char* ext = strrchr(path, '.');
    bool loaded = false;
    if(ext && strcmp(ext, ".meshx") == 0)
        loaded = model_api_find_or_load_meshx(path, &model);
    else
        loaded = model_api_find_or_load_gltf(path, &model);

    if(!loaded)
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

    GltfIndirectPush push = {0};
    push.submesh_table_ptr = slice_device_address(&renderer, assets->submesh_meta_slice);
    push.material_table_ptr = slice_device_address(&renderer, assets->material_gpu_slice);
    push.instance_ptr = slice_device_address(&renderer, queue->instance_slice);
    memcpy(push.view_proj, queue->frame_view_proj, sizeof(push.view_proj));

    vk_cmd_set_viewport_scissor(cmd, renderer.swapchain.extent);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render_pipelines.pipelines[pipelines.gltf_minimal]);
    vkCmdPushConstants(cmd, renderer.bindless_system.pipeline_layout, VK_SHADER_STAGE_ALL, 0, sizeof(GltfIndirectPush), &push);
    vkCmdDrawIndirect(cmd, queue->indirect_slice.buffer, queue->indirect_slice.offset, queue->indirect_count, sizeof(VkDrawIndirectCommand));
}

void model_api_flush_frame(VkCommandBuffer cmd)
{
    model_api_prepare_frame(cmd);
    model_api_draw_queued(cmd);
}
