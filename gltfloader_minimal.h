#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct GltfMinimalMesh
{
    float*    positions_xyz;
    float*    texcoord0_xy;
    uint32_t* indices;
    uint32_t  vertex_count;
    uint32_t  index_count;
    char*     base_color_uri;
    float     base_color_factor[4];
} GltfMinimalMesh;

bool gltf_minimal_load_first_mesh(const char* path, GltfMinimalMesh* out_mesh);
void gltf_minimal_free_mesh(GltfMinimalMesh* mesh);
