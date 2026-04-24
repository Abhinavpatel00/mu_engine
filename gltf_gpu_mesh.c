#include "gltf_gpu_mesh.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gltfloader_minimal.h"
#include "passes.h"

PUSH_CONSTANT(GltfIndirectPush, VkDeviceAddress model_table_ptr; VkDeviceAddress material_table_ptr; VkDeviceAddress instance_ptr; uint64_t pad0; float view_proj[4][4];
);

#define GLTF_INVALID_TEXTURE_ID UINT32_MAX

typedef struct GltfModelMetaGpu
{
    VkDeviceAddress pos_ptr;
    VkDeviceAddress uv_ptr;
    VkDeviceAddress idx_ptr;
    uint32_t        index_count;
    uint32_t        material_id;
    uint32_t        _pad0;
    uint32_t        _pad1;
} GltfModelMetaGpu;

typedef struct GltfDrawInstanceGpu
{
    uint32_t model_id;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
    float    model[4][4];
    float    color[4];
} GltfDrawInstanceGpu;

typedef struct GltfMaterialGpu
{
    uint32_t base_color_texture_id;
    uint32_t sampler_id;
    uint32_t _pad0;
    uint32_t _pad1;
    float    base_color_factor[4];
} GltfMaterialGpu;

typedef struct GltfModelEntry
{
    bool            alive;
    bool            uploaded;
    GltfMinimalMesh cpu;
    BufferSlice     position_slice;
    BufferSlice     uv_slice;
    BufferSlice     index_slice;
    uint32_t        material_id;
    uint32_t        base_color_texture_id;
    uint32_t        base_color_sampler_id;
} GltfModelEntry;

typedef struct GltfDrawCallCpu
{
    ModelHandle model;
    float       model_matrix[4][4];
    float       color[4];
} GltfDrawCallCpu;

typedef struct GltfModelApiState
{
    bool initialized;

    uint32_t max_models;
    uint32_t instance_capacity;

    GltfModelEntry*   models;
    GltfModelMetaGpu* model_meta_cpu;
    GltfMaterialGpu*  material_cpu;
    GltfDrawCallCpu*  draw_queue;
    GltfDrawCallCpu*  draw_sorted;
    GltfDrawInstanceGpu* instance_gpu;
    VkDrawIndirectCommand* indirect_cpu;

    BufferSlice model_meta_slice;
    BufferSlice material_slice;
    BufferSlice instance_slice;
    BufferSlice indirect_slice;

    bool     model_meta_dirty;
    bool     material_dirty;
    uint32_t draw_count;
    uint32_t prepared_instance_count;
    uint32_t prepared_cmd_count;
    bool     prepared;
    float    frame_view_proj[4][4];
} GltfModelApiState;

static GltfModelApiState g_model_api = {0};

static bool resolve_texture_path(const char* gltf_path, const char* texture_uri, char* out_path, size_t out_size)
{
    if(!gltf_path || !texture_uri || !out_path || out_size == 0)
        return false;

    if(strstr(texture_uri, "://") != NULL)
        return false;

    if(texture_uri[0] == '/')
    {
        snprintf(out_path, out_size, "%s", texture_uri);
        return true;
    }

    const char* last_slash = strrchr(gltf_path, '/');
    if(!last_slash)
    {
        snprintf(out_path, out_size, "%s", texture_uri);
        return true;
    }

    size_t dir_len = (size_t)(last_slash - gltf_path + 1);
    if(dir_len + strlen(texture_uri) + 1u > out_size)
        return false;

    memcpy(out_path, gltf_path, dir_len);
    snprintf(out_path + dir_len, out_size - dir_len, "%s", texture_uri);
    return true;
}

static int draw_call_cmp_model(const void* a, const void* b)
{
    const GltfDrawCallCpu* da = (const GltfDrawCallCpu*)a;
    const GltfDrawCallCpu* db = (const GltfDrawCallCpu*)b;
    if(da->model < db->model)
        return -1;
    if(da->model > db->model)
        return 1;
    return 0;
}

static bool upload_pending_models(VkCommandBuffer cmd)
{
    if(!g_model_api.initialized)
        return false;

    VkBufferMemoryBarrier2* barriers = (VkBufferMemoryBarrier2*)malloc(sizeof(VkBufferMemoryBarrier2) * (size_t)g_model_api.max_models * 3u);
    if(!barriers)
        return false;

    uint32_t barrier_count = 0;

    for(uint32_t i = 0; i < g_model_api.max_models; ++i)
    {
        GltfModelEntry* entry = &g_model_api.models[i];
        if(!entry->alive || entry->uploaded)
            continue;

        VkDeviceSize pos_bytes = (VkDeviceSize)entry->cpu.vertex_count * 3u * sizeof(float);
        VkDeviceSize uv_bytes  = (VkDeviceSize)entry->cpu.vertex_count * 2u * sizeof(float);
        VkDeviceSize idx_bytes = (VkDeviceSize)entry->cpu.index_count * sizeof(uint32_t);

        if(!renderer_upload_buffer_to_slice(&renderer, cmd, entry->position_slice, entry->cpu.positions_xyz, pos_bytes, 16))
            continue;
        if(!renderer_upload_buffer_to_slice(&renderer, cmd, entry->uv_slice, entry->cpu.texcoord0_xy, uv_bytes, 16))
            continue;
        if(!renderer_upload_buffer_to_slice(&renderer, cmd, entry->index_slice, entry->cpu.indices, idx_bytes, 16))
            continue;

        barriers[barrier_count++] = (VkBufferMemoryBarrier2){
            .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .buffer        = entry->position_slice.buffer,
            .offset        = entry->position_slice.offset,
            .size          = pos_bytes,
        };

        barriers[barrier_count++] = (VkBufferMemoryBarrier2){
            .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .buffer        = entry->uv_slice.buffer,
            .offset        = entry->uv_slice.offset,
            .size          = uv_bytes,
        };

        barriers[barrier_count++] = (VkBufferMemoryBarrier2){
            .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .buffer        = entry->index_slice.buffer,
            .offset        = entry->index_slice.offset,
            .size          = idx_bytes,
        };

        g_model_api.model_meta_cpu[i].pos_ptr     = slice_device_address(&renderer, entry->position_slice);
        g_model_api.model_meta_cpu[i].uv_ptr      = slice_device_address(&renderer, entry->uv_slice);
        g_model_api.model_meta_cpu[i].idx_ptr     = slice_device_address(&renderer, entry->index_slice);
        g_model_api.model_meta_cpu[i].index_count = entry->cpu.index_count;
        g_model_api.model_meta_cpu[i].material_id = entry->material_id;

        g_model_api.material_cpu[entry->material_id].base_color_texture_id = entry->base_color_texture_id;
        g_model_api.material_cpu[entry->material_id].sampler_id            = entry->base_color_sampler_id;
        memcpy(g_model_api.material_cpu[entry->material_id].base_color_factor, entry->cpu.base_color_factor,
               sizeof(entry->cpu.base_color_factor));

        entry->uploaded                            = true;
        g_model_api.model_meta_dirty               = true;
        g_model_api.material_dirty                 = true;
    }

    if(barrier_count > 0)
    {
        VkDependencyInfo dep = {
            .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = barrier_count,
            .pBufferMemoryBarriers    = barriers,
        };
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    free(barriers);

    if(g_model_api.model_meta_dirty)
    {
        VkDeviceSize bytes = (VkDeviceSize)g_model_api.max_models * sizeof(GltfModelMetaGpu);
        if(renderer_upload_buffer_to_slice(&renderer, cmd, g_model_api.model_meta_slice, g_model_api.model_meta_cpu, bytes, 16))
        {
            VkBufferMemoryBarrier2 meta_barrier = {
                .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                .buffer        = g_model_api.model_meta_slice.buffer,
                .offset        = g_model_api.model_meta_slice.offset,
                .size          = bytes,
            };

            VkDependencyInfo dep = {
                .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .bufferMemoryBarrierCount = 1,
                .pBufferMemoryBarriers    = &meta_barrier,
            };
            vkCmdPipelineBarrier2(cmd, &dep);
            g_model_api.model_meta_dirty = false;
        }
    }

    if(g_model_api.material_dirty)
    {
        VkDeviceSize bytes = (VkDeviceSize)g_model_api.max_models * sizeof(GltfMaterialGpu);
        if(renderer_upload_buffer_to_slice(&renderer, cmd, g_model_api.material_slice, g_model_api.material_cpu, bytes, 16))
        {
            VkBufferMemoryBarrier2 material_barrier = {
                .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                .buffer        = g_model_api.material_slice.buffer,
                .offset        = g_model_api.material_slice.offset,
                .size          = bytes,
            };

            VkDependencyInfo dep = {
                .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .bufferMemoryBarrierCount = 1,
                .pBufferMemoryBarriers    = &material_barrier,
            };
            vkCmdPipelineBarrier2(cmd, &dep);
            g_model_api.material_dirty = false;
        }
    }

    return true;
}

bool model_api_init(uint32_t max_models, uint32_t instance_capacity)
{
    if(g_model_api.initialized || max_models == 0 || instance_capacity == 0)
        return false;

    memset(&g_model_api, 0, sizeof(g_model_api));

    g_model_api.max_models        = max_models;
    g_model_api.instance_capacity = instance_capacity;
    g_model_api.models            = (GltfModelEntry*)calloc(max_models, sizeof(GltfModelEntry));
    g_model_api.model_meta_cpu    = (GltfModelMetaGpu*)calloc(max_models, sizeof(GltfModelMetaGpu));
    g_model_api.material_cpu      = (GltfMaterialGpu*)calloc(max_models, sizeof(GltfMaterialGpu));
    g_model_api.draw_queue        = (GltfDrawCallCpu*)calloc(instance_capacity, sizeof(GltfDrawCallCpu));
    g_model_api.draw_sorted       = (GltfDrawCallCpu*)calloc(instance_capacity, sizeof(GltfDrawCallCpu));
    g_model_api.instance_gpu      = (GltfDrawInstanceGpu*)calloc(instance_capacity, sizeof(GltfDrawInstanceGpu));
    g_model_api.indirect_cpu      = (VkDrawIndirectCommand*)calloc(instance_capacity, sizeof(VkDrawIndirectCommand));

    if(!g_model_api.models || !g_model_api.model_meta_cpu || !g_model_api.material_cpu || !g_model_api.draw_queue || !g_model_api.draw_sorted || !g_model_api.instance_gpu || !g_model_api.indirect_cpu)
    {
        model_api_shutdown();
        return false;
    }

    g_model_api.model_meta_slice = buffer_pool_alloc(&renderer.gpu_pool, (VkDeviceSize)max_models * sizeof(GltfModelMetaGpu), 16);
    g_model_api.material_slice   = buffer_pool_alloc(&renderer.gpu_pool, (VkDeviceSize)max_models * sizeof(GltfMaterialGpu), 16);
    g_model_api.instance_slice   = buffer_pool_alloc(&renderer.gpu_pool, (VkDeviceSize)instance_capacity * sizeof(GltfDrawInstanceGpu), 16);
    g_model_api.indirect_slice   = buffer_pool_alloc(&renderer.gpu_pool, (VkDeviceSize)instance_capacity * sizeof(VkDrawIndirectCommand), 16);

    if(g_model_api.model_meta_slice.buffer == VK_NULL_HANDLE || g_model_api.material_slice.buffer == VK_NULL_HANDLE
       || g_model_api.instance_slice.buffer == VK_NULL_HANDLE || g_model_api.indirect_slice.buffer == VK_NULL_HANDLE)
    {
        model_api_shutdown();
        return false;
    }

    g_model_api.initialized      = true;
    g_model_api.model_meta_dirty = true;
    g_model_api.material_dirty   = true;

    for(uint32_t i = 0; i < max_models; ++i)
    {
        g_model_api.material_cpu[i].base_color_texture_id = GLTF_INVALID_TEXTURE_ID;
        g_model_api.material_cpu[i].sampler_id            = renderer.default_samplers.samplers[SAMPLER_LINEAR_WRAP];
        g_model_api.material_cpu[i].base_color_factor[0]  = 1.0f;
        g_model_api.material_cpu[i].base_color_factor[1]  = 1.0f;
        g_model_api.material_cpu[i].base_color_factor[2]  = 1.0f;
        g_model_api.material_cpu[i].base_color_factor[3]  = 1.0f;
    }
    return true;
}

void model_api_shutdown(void)
{
    if(g_model_api.model_meta_slice.buffer != VK_NULL_HANDLE)
        buffer_pool_free(g_model_api.model_meta_slice);
    if(g_model_api.material_slice.buffer != VK_NULL_HANDLE)
        buffer_pool_free(g_model_api.material_slice);
    if(g_model_api.instance_slice.buffer != VK_NULL_HANDLE)
        buffer_pool_free(g_model_api.instance_slice);
    if(g_model_api.indirect_slice.buffer != VK_NULL_HANDLE)
        buffer_pool_free(g_model_api.indirect_slice);

    if(g_model_api.models)
    {
        for(uint32_t i = 0; i < g_model_api.max_models; ++i)
        {
            GltfModelEntry* entry = &g_model_api.models[i];
            if(entry->position_slice.buffer != VK_NULL_HANDLE)
                buffer_pool_free(entry->position_slice);
            if(entry->uv_slice.buffer != VK_NULL_HANDLE)
                buffer_pool_free(entry->uv_slice);
            if(entry->index_slice.buffer != VK_NULL_HANDLE)
                buffer_pool_free(entry->index_slice);
            if(entry->base_color_texture_id != GLTF_INVALID_TEXTURE_ID)
                destroy_texture(&renderer, entry->base_color_texture_id);
            gltf_minimal_free_mesh(&entry->cpu);
        }
    }

    free(g_model_api.indirect_cpu);
    free(g_model_api.instance_gpu);
    free(g_model_api.draw_sorted);
    free(g_model_api.draw_queue);
    free(g_model_api.material_cpu);
    free(g_model_api.model_meta_cpu);
    free(g_model_api.models);

    memset(&g_model_api, 0, sizeof(g_model_api));
}

bool model_api_load_gltf(const char* path, ModelHandle* out_model)
{
    if(!g_model_api.initialized || !path || !out_model)
        return false;

    uint32_t slot = MODEL_HANDLE_INVALID;
    for(uint32_t i = 0; i < g_model_api.max_models; ++i)
    {
        if(!g_model_api.models[i].alive)
        {
            slot = i;
            break;
        }
    }

    if(slot == MODEL_HANDLE_INVALID)
        return false;

    GltfModelEntry* entry = &g_model_api.models[slot];
    memset(entry, 0, sizeof(*entry));

    if(!gltf_minimal_load_first_mesh(path, &entry->cpu))
        return false;

    VkDeviceSize pos_bytes = (VkDeviceSize)entry->cpu.vertex_count * 3u * sizeof(float);
    VkDeviceSize uv_bytes  = (VkDeviceSize)entry->cpu.vertex_count * 2u * sizeof(float);
    VkDeviceSize idx_bytes = (VkDeviceSize)entry->cpu.index_count * sizeof(uint32_t);

    entry->position_slice = buffer_pool_alloc(&renderer.gpu_pool, pos_bytes, 16);
    entry->uv_slice       = buffer_pool_alloc(&renderer.gpu_pool, uv_bytes, 16);
    entry->index_slice    = buffer_pool_alloc(&renderer.gpu_pool, idx_bytes, 16);
    if(entry->position_slice.buffer == VK_NULL_HANDLE || entry->uv_slice.buffer == VK_NULL_HANDLE || entry->index_slice.buffer == VK_NULL_HANDLE)
    {
        if(entry->position_slice.buffer != VK_NULL_HANDLE)
            buffer_pool_free(entry->position_slice);
        if(entry->uv_slice.buffer != VK_NULL_HANDLE)
            buffer_pool_free(entry->uv_slice);
        if(entry->index_slice.buffer != VK_NULL_HANDLE)
            buffer_pool_free(entry->index_slice);
        gltf_minimal_free_mesh(&entry->cpu);
        memset(entry, 0, sizeof(*entry));
        return false;
    }

    entry->base_color_texture_id = GLTF_INVALID_TEXTURE_ID;
    entry->base_color_sampler_id = renderer.default_samplers.samplers[SAMPLER_LINEAR_WRAP];

    if(entry->cpu.base_color_uri)
    {
        char texture_path[PATH_MAX];
        if(resolve_texture_path(path, entry->cpu.base_color_uri, texture_path, sizeof(texture_path)))
        {
            TextureID tex = load_texture(&renderer, texture_path);
            if(tex != GLTF_INVALID_TEXTURE_ID)
                entry->base_color_texture_id = tex;
        }
    }

    entry->alive       = true;
    entry->uploaded    = false;
    entry->material_id = slot;
    *out_model         = slot;
    return true;
}

void model_api_begin_frame(const Camera* cam)
{
    if(!g_model_api.initialized || !cam)
        return;

    memcpy(g_model_api.frame_view_proj, cam->view_proj, sizeof(g_model_api.frame_view_proj));
    g_model_api.draw_count              = 0;
    g_model_api.prepared_instance_count = 0;
    g_model_api.prepared_cmd_count      = 0;
    g_model_api.prepared                = false;
}

bool draw3d(ModelHandle model, const float model_matrix[4][4], const float color[4])
{
    if(!g_model_api.initialized || model == MODEL_HANDLE_INVALID || model >= g_model_api.max_models)
        return false;
    if(g_model_api.draw_count >= g_model_api.instance_capacity)
        return false;
    if(!g_model_api.models[model].alive)
        return false;

    GltfDrawCallCpu* call = &g_model_api.draw_queue[g_model_api.draw_count++];
    call->model           = model;
    memcpy(call->model_matrix, model_matrix, sizeof(call->model_matrix));
    memcpy(call->color, color, sizeof(call->color));
    return true;
}

void model_api_prepare_frame(VkCommandBuffer cmd)
{
    if(!g_model_api.initialized)
        return;

    if(!upload_pending_models(cmd))
        return;

    if(g_model_api.draw_count == 0)
    {
        g_model_api.prepared_instance_count = 0;
        g_model_api.prepared_cmd_count      = 0;
        g_model_api.prepared                = true;
        return;
    }

    memcpy(g_model_api.draw_sorted, g_model_api.draw_queue, (size_t)g_model_api.draw_count * sizeof(GltfDrawCallCpu));
    qsort(g_model_api.draw_sorted, g_model_api.draw_count, sizeof(GltfDrawCallCpu), draw_call_cmp_model);

    uint32_t valid_count = 0;
    for(uint32_t i = 0; i < g_model_api.draw_count; ++i)
    {
        const GltfDrawCallCpu* src = &g_model_api.draw_sorted[i];
        if(src->model >= g_model_api.max_models)
            continue;
        const GltfModelEntry* entry = &g_model_api.models[src->model];
        if(!entry->alive || !entry->uploaded)
            continue;

        GltfDrawInstanceGpu* dst = &g_model_api.instance_gpu[valid_count++];
        dst->model_id            = src->model;
        dst->_pad0               = 0;
        dst->_pad1               = 0;
        dst->_pad2               = 0;
        memcpy(dst->model, src->model_matrix, sizeof(dst->model));
        memcpy(dst->color, src->color, sizeof(dst->color));
    }

    if(valid_count == 0)
    {
        g_model_api.prepared_instance_count = 0;
        g_model_api.prepared_cmd_count      = 0;
        g_model_api.prepared                = true;
        return;
    }

    uint32_t cmd_count = 0;
    uint32_t run_start = 0;
    while(run_start < valid_count)
    {
        uint32_t model_id = g_model_api.instance_gpu[run_start].model_id;
        uint32_t run_end  = run_start + 1;
        while(run_end < valid_count && g_model_api.instance_gpu[run_end].model_id == model_id)
            ++run_end;

        VkDrawIndirectCommand* out_cmd = &g_model_api.indirect_cpu[cmd_count++];
        out_cmd->vertexCount           = g_model_api.model_meta_cpu[model_id].index_count;
        out_cmd->instanceCount         = run_end - run_start;
        out_cmd->firstVertex           = 0;
        out_cmd->firstInstance         = run_start;

        run_start = run_end;
    }

    VkDeviceSize instance_bytes = (VkDeviceSize)valid_count * sizeof(GltfDrawInstanceGpu);
    VkDeviceSize indirect_bytes = (VkDeviceSize)cmd_count * sizeof(VkDrawIndirectCommand);

    if(!renderer_upload_buffer_to_slice(&renderer, cmd, g_model_api.instance_slice, g_model_api.instance_gpu, instance_bytes, 16))
        return;
    if(!renderer_upload_buffer_to_slice(&renderer, cmd, g_model_api.indirect_slice, g_model_api.indirect_cpu, indirect_bytes, 16))
        return;

    VkBufferMemoryBarrier2 barriers[2] = {
        {
            .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .buffer        = g_model_api.instance_slice.buffer,
            .offset        = g_model_api.instance_slice.offset,
            .size          = instance_bytes,
        },
        {
            .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
            .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
            .buffer        = g_model_api.indirect_slice.buffer,
            .offset        = g_model_api.indirect_slice.offset,
            .size          = indirect_bytes,
        },
    };
    VkDependencyInfo dep = {
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 2,
        .pBufferMemoryBarriers    = barriers,
    };
    vkCmdPipelineBarrier2(cmd, &dep);

    g_model_api.prepared_instance_count = valid_count;
    g_model_api.prepared_cmd_count      = cmd_count;
    g_model_api.prepared                = true;
}

void model_api_draw_queued(VkCommandBuffer cmd)
{
    if(!g_model_api.initialized || !g_model_api.prepared || g_model_api.prepared_cmd_count == 0 || g_model_api.prepared_instance_count == 0)
        return;

    GltfIndirectPush push = {0};
    push.model_table_ptr    = slice_device_address(&renderer, g_model_api.model_meta_slice);
    push.material_table_ptr = slice_device_address(&renderer, g_model_api.material_slice);
    push.instance_ptr       = slice_device_address(&renderer, g_model_api.instance_slice);
    memcpy(push.view_proj, g_model_api.frame_view_proj, sizeof(push.view_proj));

    vk_cmd_set_viewport_scissor(cmd, renderer.swapchain.extent);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render_pipelines.pipelines[pipelines.gltf_minimal]);
    vkCmdPushConstants(cmd, renderer.bindless_system.pipeline_layout, VK_SHADER_STAGE_ALL, 0, sizeof(GltfIndirectPush), &push);
    vkCmdDrawIndirect(cmd, g_model_api.indirect_slice.buffer, g_model_api.indirect_slice.offset,
                      g_model_api.prepared_cmd_count, sizeof(VkDrawIndirectCommand));
}

void model_api_flush_frame(VkCommandBuffer cmd)
{
    model_api_prepare_frame(cmd);
    model_api_draw_queued(cmd);
}
