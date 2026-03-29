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

    if(data->meshes_count == 0 || data->meshes[0].primitives_count == 0)
    {
        cgltf_free(data);
        return false;
    }

    cgltf_primitive* prim = &data->meshes[0].primitives[0];
    if(!prim->indices)
    {
        cgltf_free(data);
        return false;
    }

    cgltf_accessor* pos_accessor = NULL;
    for(size_t i = 0; i < prim->attributes_count; ++i)
    {
        if(prim->attributes[i].type == cgltf_attribute_type_position)
        {
            pos_accessor = prim->attributes[i].data;
            break;
        }
    }

    if(!pos_accessor || pos_accessor->count == 0 || !prim->indices->count)
    {
        cgltf_free(data);
        return false;
    }

    uint32_t vertex_count = (uint32_t)pos_accessor->count;
    uint32_t index_count  = (uint32_t)prim->indices->count;

    float* positions = (float*)malloc((size_t)vertex_count * 3u * sizeof(float));
    uint32_t* indices = (uint32_t*)malloc((size_t)index_count * sizeof(uint32_t));
    if(!positions || !indices)
    {
        free(positions);
        free(indices);
        cgltf_free(data);
        return false;
    }

    for(uint32_t i = 0; i < vertex_count; ++i)
    {
        float p[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        cgltf_accessor_read_float(pos_accessor, i, p, 3);
        positions[i * 3u + 0u] = p[0];
        positions[i * 3u + 1u] = p[1];
        positions[i * 3u + 2u] = p[2];
    }

    for(uint32_t i = 0; i < index_count; ++i)
    {
        indices[i] = (uint32_t)cgltf_accessor_read_index(prim->indices, i);
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
