#include "gltfloader_minimal.h"

#include <stdlib.h>
#include <string.h>

#include "external/cgltf/cgltf.h"

static void gltf_minimal_reset_mesh(GltfMinimalMesh* mesh)
{
    mesh->positions_xyz = NULL;
    mesh->indices       = NULL;
    mesh->vertex_count  = 0;
    mesh->index_count   = 0;
}

static cgltf_accessor* find_position_accessor(const cgltf_primitive* prim)
{
    for(size_t i = 0; i < prim->attributes_count; ++i)
    {
        if(prim->attributes[i].type == cgltf_attribute_type_position)
            return prim->attributes[i].data;
    }
    return NULL;
}

static void count_node_triangles(const cgltf_node* node, uint64_t* vertex_count, uint64_t* index_count)
{
    if(node->mesh)
    {
        for(size_t prim_i = 0; prim_i < node->mesh->primitives_count; ++prim_i)
        {
            const cgltf_primitive* prim = &node->mesh->primitives[prim_i];
            if(prim->type != cgltf_primitive_type_triangles)
                continue;

            const cgltf_accessor* pos_accessor = find_position_accessor(prim);
            if(!pos_accessor || pos_accessor->count == 0)
                continue;

            uint64_t vtx = (uint64_t)pos_accessor->count;
            uint64_t idx = prim->indices ? (uint64_t)prim->indices->count : vtx;
            if(idx == 0)
                continue;

            *vertex_count += vtx;
            *index_count += idx;
        }
    }

    for(size_t child_i = 0; child_i < node->children_count; ++child_i)
        count_node_triangles(node->children[child_i], vertex_count, index_count);
}

static bool append_node_triangles(const cgltf_node* node, float* positions, uint32_t* indices, uint32_t* vertex_cursor, uint32_t* index_cursor)
{
    float world[16];
    cgltf_node_transform_world(node, world);

    if(node->mesh)
    {
        for(size_t prim_i = 0; prim_i < node->mesh->primitives_count; ++prim_i)
        {
            const cgltf_primitive* prim = &node->mesh->primitives[prim_i];
            if(prim->type != cgltf_primitive_type_triangles)
                continue;

            const cgltf_accessor* pos_accessor = find_position_accessor(prim);
            if(!pos_accessor || pos_accessor->count == 0)
                continue;

            uint32_t vtx = (uint32_t)pos_accessor->count;
            uint32_t idx = prim->indices ? (uint32_t)prim->indices->count : vtx;
            if(idx == 0)
                continue;

            float* dst_pos = positions + (size_t)(*vertex_cursor) * 3u;
            cgltf_size unpacked_pos = cgltf_accessor_unpack_floats(pos_accessor, dst_pos, (cgltf_size)vtx * 3u);
            if(unpacked_pos < (cgltf_size)vtx * 3u)
                return false;

            for(uint32_t i = 0; i < vtx; ++i)
            {
                float x = dst_pos[i * 3u + 0u];
                float y = dst_pos[i * 3u + 1u];
                float z = dst_pos[i * 3u + 2u];

                dst_pos[i * 3u + 0u] = x * world[0] + y * world[4] + z * world[8] + world[12];
                dst_pos[i * 3u + 1u] = x * world[1] + y * world[5] + z * world[9] + world[13];
                dst_pos[i * 3u + 2u] = x * world[2] + y * world[6] + z * world[10] + world[14];
            }

            uint32_t* dst_idx = indices + *index_cursor;
            if(prim->indices)
            {
                cgltf_size unpacked_idx = cgltf_accessor_unpack_indices(prim->indices, dst_idx, sizeof(uint32_t), idx);
                if(unpacked_idx < idx)
                    return false;
                for(uint32_t i = 0; i < idx; ++i)
                    dst_idx[i] += *vertex_cursor;
            }
            else
            {
                for(uint32_t i = 0; i < idx; ++i)
                    dst_idx[i] = *vertex_cursor + i;
            }

            *vertex_cursor += vtx;
            *index_cursor += idx;
        }
    }

    for(size_t child_i = 0; child_i < node->children_count; ++child_i)
    {
        if(!append_node_triangles(node->children[child_i], positions, indices, vertex_cursor, index_cursor))
            return false;
    }

    return true;
}

static void count_all_mesh_triangles(const cgltf_data* data, uint64_t* vertex_count, uint64_t* index_count)
{
    for(size_t mesh_i = 0; mesh_i < data->meshes_count; ++mesh_i)
    {
        const cgltf_mesh* mesh = &data->meshes[mesh_i];
        for(size_t prim_i = 0; prim_i < mesh->primitives_count; ++prim_i)
        {
            const cgltf_primitive* prim = &mesh->primitives[prim_i];
            if(prim->type != cgltf_primitive_type_triangles)
                continue;

            const cgltf_accessor* pos_accessor = find_position_accessor(prim);
            if(!pos_accessor || pos_accessor->count == 0)
                continue;

            uint64_t vtx = (uint64_t)pos_accessor->count;
            uint64_t idx = prim->indices ? (uint64_t)prim->indices->count : vtx;
            if(idx == 0)
                continue;

            *vertex_count += vtx;
            *index_count += idx;
        }
    }
}

static bool append_all_mesh_triangles(const cgltf_data* data, float* positions, uint32_t* indices, uint32_t* vertex_cursor, uint32_t* index_cursor)
{
    for(size_t mesh_i = 0; mesh_i < data->meshes_count; ++mesh_i)
    {
        const cgltf_mesh* mesh = &data->meshes[mesh_i];
        for(size_t prim_i = 0; prim_i < mesh->primitives_count; ++prim_i)
        {
            const cgltf_primitive* prim = &mesh->primitives[prim_i];
            if(prim->type != cgltf_primitive_type_triangles)
                continue;

            const cgltf_accessor* pos_accessor = find_position_accessor(prim);
            if(!pos_accessor || pos_accessor->count == 0)
                continue;

            uint32_t vtx = (uint32_t)pos_accessor->count;
            uint32_t idx = prim->indices ? (uint32_t)prim->indices->count : vtx;
            if(idx == 0)
                continue;

            float* dst_pos = positions + (size_t)(*vertex_cursor) * 3u;
            cgltf_size unpacked_pos = cgltf_accessor_unpack_floats(pos_accessor, dst_pos, (cgltf_size)vtx * 3u);
            if(unpacked_pos < (cgltf_size)vtx * 3u)
                return false;

            uint32_t* dst_idx = indices + *index_cursor;
            if(prim->indices)
            {
                cgltf_size unpacked_idx = cgltf_accessor_unpack_indices(prim->indices, dst_idx, sizeof(uint32_t), idx);
                if(unpacked_idx < idx)
                    return false;
                for(uint32_t i = 0; i < idx; ++i)
                    dst_idx[i] += *vertex_cursor;
            }
            else
            {
                for(uint32_t i = 0; i < idx; ++i)
                    dst_idx[i] = *vertex_cursor + i;
            }

            *vertex_cursor += vtx;
            *index_cursor += idx;
        }
    }

    return true;
}

bool gltf_minimal_load_first_mesh(const char* path, GltfMinimalMesh* out_mesh)
{
    if(!path || !out_mesh)
        return false;

    gltf_minimal_reset_mesh(out_mesh);

    cgltf_options options = {0};
    cgltf_data*   data    = NULL;

    if(cgltf_parse_file(&options, path, &data) != cgltf_result_success)
        return false;

    if(cgltf_load_buffers(&options, data, path) != cgltf_result_success)
    {
        cgltf_free(data);
        return false;
    }

    if(cgltf_validate(data) != cgltf_result_success)
    {
        cgltf_free(data);
        return false;
    }

    if(data->meshes_count == 0)
    {
        cgltf_free(data);
        return false;
    }

    uint64_t vertex_count_u64 = 0;
    uint64_t index_count_u64  = 0;
    bool used_scene_nodes     = false;

    const cgltf_scene* scene = data->scene;
    if(!scene && data->scenes_count > 0)
        scene = &data->scenes[0];

    if(scene && scene->nodes_count > 0)
    {
        used_scene_nodes = true;
        for(size_t i = 0; i < scene->nodes_count; ++i)
            count_node_triangles(scene->nodes[i], &vertex_count_u64, &index_count_u64);
    }
    else
    {
        count_all_mesh_triangles(data, &vertex_count_u64, &index_count_u64);
    }

    if(vertex_count_u64 == 0 || index_count_u64 == 0 || vertex_count_u64 > UINT32_MAX || index_count_u64 > UINT32_MAX)
    {
        cgltf_free(data);
        return false;
    }

    uint32_t vertex_count = (uint32_t)vertex_count_u64;
    uint32_t index_count  = (uint32_t)index_count_u64;

    float* positions = (float*)malloc((size_t)vertex_count * 3u * sizeof(float));
    uint32_t* indices = (uint32_t*)malloc((size_t)index_count * sizeof(uint32_t));
    if(!positions || !indices)
    {
        free(positions);
        free(indices);
        cgltf_free(data);
        return false;
    }

    uint32_t vertex_cursor = 0;
    uint32_t index_cursor  = 0;
    bool append_ok         = false;

    if(used_scene_nodes)
    {
        append_ok = true;
        for(size_t i = 0; i < scene->nodes_count; ++i)
        {
            if(!append_node_triangles(scene->nodes[i], positions, indices, &vertex_cursor, &index_cursor))
            {
                append_ok = false;
                break;
            }
        }
    }
    else
    {
        append_ok = append_all_mesh_triangles(data, positions, indices, &vertex_cursor, &index_cursor);
    }

    if(!append_ok || vertex_cursor != vertex_count || index_cursor != index_count)
    {
        free(positions);
        free(indices);
        cgltf_free(data);
        return false;
    }

    out_mesh->positions_xyz = positions;
    out_mesh->indices       = indices;
    out_mesh->vertex_count  = vertex_count;
    out_mesh->index_count   = index_count;

    cgltf_free(data);
    return true;
}

void gltf_minimal_free_mesh(GltfMinimalMesh* mesh)
{
    if(!mesh)
        return;

    free(mesh->positions_xyz);
    free(mesh->indices);
    gltf_minimal_reset_mesh(mesh);
}
