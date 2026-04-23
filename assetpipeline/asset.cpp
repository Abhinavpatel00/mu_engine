#define MU_IMPLEMENTATION
#include "../mu/mu.h"

typedef struct
{
    uint16_t vx, vy, vz;
    uint16_t tp;      // tangent
    uint32_t np;      // normal
    uint16_t tu, tv;  // uv
} PackedVertex;

MU_INLINE PackedVertex mu_pack_vertex(float px, float py, float pz, float nx, float ny, float nz, float tx, float ty, float u, float v, int bitangent_sign)
{
    PackedVertex pv;

    pv.vx = mu_quantize_half(px);
    pv.vy = mu_quantize_half(py);
    pv.vz = mu_quantize_half(pz);

    int qnx = mu_quantize_snorm(nx, 10);
    int qny = mu_quantize_snorm(ny, 10);
    int qnz = mu_quantize_snorm(nz, 10);

    pv.np = ((uint32_t)(qnx & 1023)) | ((uint32_t)(qny & 1023) << 10) | ((uint32_t)(qnz & 1023) << 20)
            | ((uint32_t)(bitangent_sign & 3) << 30);

    int qtx = mu_quantize_snorm(tx, 8);
    int qty = mu_quantize_snorm(ty, 8);

    pv.tp = ((uint16_t)(qtx & 255) << 8) | ((uint16_t)(qty & 255));

    pv.tu = mu_quantize_half(u);
    pv.tv = mu_quantize_half(v);

    return pv;
}
/*
{
  "meshes": [
    {
      "name": "Cube",

      "vertices": {
        "positions": [x, y, z, ...],
        "normals":   [x, y, z, ...],
        "uvs":       [u, v, ...],
        "tangents":  [x, y, z, w, ...]
      },

      "indices": [0, 1, 2, 2, 3, 0],

      "material": 0
    }
  ],

  "materials": [
    {
      "name": "Default",
      "baseColor": [1.0, 1.0, 1.0],
      "baseColorTexture": "albedo.png"
    }
  ]
}
*/
/*
 GLB binary blob (big pile of bytes)
        ↓
buffer (entire file chunk)
        ↓
buffer_view (slice of that blob)
        ↓
accessor (how to interpret that slice)

    BUFFER (entire binary chunk)
    ┌──────────────────────────────┐
    │ random binary soup           │
    └──────────────────────────────┘
                 │
                 ▼

    BUFFER VIEW (slice of buffer)
    ┌───────────────┐
    │ offset        │  ← where to start
    │ size          │  ← how much to read
    │ stride        │  ← spacing between elements
    └───────────────┘


   */
#define CGLTF_IMPLEMENTATION
#include <stdio.h>
#include <inttypes.h>

#include "../external/cgltf/cgltf.h"
static const char* cgltf_file_type_str(cgltf_file_type t)
{
    switch(t)
    {
        case cgltf_file_type_invalid:
            return "invalid";
        case cgltf_file_type_gltf:
            return "gltf";
        case cgltf_file_type_glb:
            return "glb";
        default:
            return "unknown";
    }
}

void debug_print_cgltf_data(const cgltf_data* d)
{
    if(!d)
    {
        printf("cgltf_data = NULL\n");
        return;
    }

    printf("\n========== GLTF DEBUG DUMP ==========\n");

    // FILE INFO
    printf("\n[File]\n");
    printf("  type        : %s\n", cgltf_file_type_str(d->file_type));
    printf("  file_data   : %p\n", d->file_data);
    printf("  file_size   : %zu bytes\n", d->file_size);

    // JSON + BIN
    printf("\n[Raw Chunks]\n");
    printf("  json        : %p (%zu bytes)\n", d->json, d->json_size);
    printf("  bin         : %p (%zu bytes)\n", d->bin, d->bin_size);

    // CORE COUNTS
    printf("\n[Counts]\n");
    printf("  meshes      : %zu\n", d->meshes_count);
    printf("  materials   : %zu\n", d->materials_count);
    printf("  accessors   : %zu\n", d->accessors_count);
    printf("  bufferViews : %zu\n", d->buffer_views_count);
    printf("  buffers     : %zu\n", d->buffers_count);
    printf("  images      : %zu\n", d->images_count);
    printf("  textures    : %zu\n", d->textures_count);
    printf("  samplers    : %zu\n", d->samplers_count);
    printf("  skins       : %zu\n", d->skins_count);
    printf("  cameras     : %zu\n", d->cameras_count);
    printf("  lights      : %zu\n", d->lights_count);
    printf("  nodes       : %zu\n", d->nodes_count);
    printf("  scenes      : %zu\n", d->scenes_count);
    printf("  animations  : %zu\n", d->animations_count);
    printf("  variants    : %zu\n", d->variants_count);

    // DEFAULT SCENE
    printf("\n[Scene]\n");
    printf("  default scene pointer : %p\n", (void*)d->scene);

    // EXTENSIONS
    printf("\n[Extensions]\n");
    printf("  data_extensions_count : %zu\n", d->data_extensions_count);
    printf("  used extensions       : %zu\n", d->extensions_used_count);
    for(cgltf_size i = 0; i < d->extensions_used_count; i++)
    {
        printf("    used[%zu] = %s\n", i, d->extensions_used[i]);
    }

    printf("  required extensions   : %zu\n", d->extensions_required_count);
    for(cgltf_size i = 0; i < d->extensions_required_count; i++)
    {
        printf("    required[%zu] = %s\n", i, d->extensions_required[i]);
    }

    // // BUFFERS (IMPORTANT)
    // printf("\n[Buffers]\n");
    // for(cgltf_size i = 0; i < d->buffers_count; i++)
    // {
    //     const cgltf_buffer* b = &d->buffers[i];
    //     printf("  buffer[%zu]: size=%zu data=%p uri=%s\n", i, b->size, b->data, b->uri ? b->uri : "NULL");
    // }
    //
    // // BUFFER VIEWS
    // printf("\n[BufferViews]\n");
    // for(cgltf_size i = 0; i < d->buffer_views_count; i++)
    // {
    //     const cgltf_buffer_view* bv = &d->buffer_views[i];
    //     printf("  view[%zu]: offset=%zu size=%zu stride=%zu buffer=%p\n", i, bv->offset, bv->size, bv->stride, (void*)bv->buffer);
    // }
    //
    // // ACCESSORS (THIS IS WHERE PEOPLE USUALLY MESS UP)
    // printf("\n[Accessors]\n");
    // for(cgltf_size i = 0; i < d->accessors_count; i++)
    // {
    //     const cgltf_accessor* a = &d->accessors[i];
    //     printf("  accessor[%zu]: count=%zu stride=%zu offset=%zu buffer_view=%p\n", i, a->count, a->stride, a->offset,
    //            (void*)a->buffer_view);
    // }

    printf("\n=====================================\n");
}
/*
	animation
 ├── skeletal (skins + joints)
 ├── node transform animation   <-- THIS is what you have
 └── morph target animation








*/
using namespace std;


void process_mesh(cgltf_mesh* mesh) {
    cout << "Mesh: " << (mesh->name ? mesh->name : "unnamed") << "\n";

    for (size_t p = 0; p < mesh->primitives_count; ++p) {
        cgltf_primitive& prim = mesh->primitives[p];

        bool has_pos = false, has_norm = false, has_uv = false, has_tangent = false;

        // Detect attributes
        for (size_t i = 0; i < prim.attributes_count; ++i) {
            cgltf_attribute& attr = prim.attributes[i];

            if (attr.type == cgltf_attribute_type_position) has_pos = true;
            if (attr.type == cgltf_attribute_type_normal)   has_norm = true;
            if (attr.type == cgltf_attribute_type_texcoord) has_uv = true;
            if (attr.type == cgltf_attribute_type_tangent)  has_tangent = true;
        }

        size_t vertex_count = 0;
        if (prim.attributes_count > 0) {
            vertex_count = prim.attributes[0].data->count;
        }

        size_t index_count = prim.indices ? prim.indices->count : 0;

        cout << "  Vertices: " << vertex_count << "\n";
        cout << "  Indices : " << index_count << "\n";

        cout << "  Attributes:\n";
        cout << "    POSITION   " << (has_pos ? "✓" : "✗") << "\n";
        cout << "    NORMAL     " << (has_norm ? "✓" : "✗") << "\n";
        cout << "    TANGENT    " << (has_tangent ? "✓" : "✗") << "\n";
        cout << "    TEXCOORD_0 " << (has_uv ? "✓" : "✗") << "\n";

        /*
        ==========================================================
        Extract index buffer (required for meshopt)
        ==========================================================
        */

        vector<unsigned int> indices(index_count);
        if (prim.indices) {
            for (size_t i = 0; i < index_count; ++i) {
                indices[i] = (unsigned int)cgltf_accessor_read_index(prim.indices, i);
            }
        }

        /*
        ==========================================================
        meshoptimizer magic (cache optimization)
        ==========================================================
        */

        vector<unsigned int> optimized = indices;

        if (!indices.empty()) {
            meshopt_optimizeVertexCache(
                optimized.data(),
                indices.data(),
                index_count,
                vertex_count
            );
        }

        cout << "  After Optimization:\n";
        cout << "    Indices reordered for GPU cache\n";

        /*
        ==========================================================
        Warnings (your reality check)
        ==========================================================
        */

        if (!has_norm)    cout << "  ⚠ Missing normals\n";
        if (!has_tangent) cout << "  ⚠ Missing tangents\n";
        if (!has_uv)      cout << "  ⚠ Missing UVs\n";

        cout << "\n";
    }
}

#define GLTF "../assets/3DGodotRobot.glb"
int main() {
    cgltf_options options = {};
    cgltf_data* data = nullptr;

    cgltf_result result = cgltf_parse_file(&options, "model.gltf", &data);
    if (result != cgltf_result_success) {
        cout << "Failed to parse\n";
        return -1;
    }

    cgltf_load_buffers(&options, data, "model.gltf");

    /*
    ==========================================================
    Scene traversal (simple version)
    ==========================================================
    */

    for (size_t i = 0; i < data->meshes_count; ++i) {
        process_mesh(&data->meshes[i]);
    }

    cgltf_free(data);
    return 0;
}

// int main()
// {
//     cgltf_data*   data    = NULL;
//     cgltf_options options = {0};
//
//
//     cgltf_parse_file(&options, GLTF, &data);
//     cgltf_load_buffers(&options, data,GLTF);
//
//     debug_print_cgltf_data(data);
//
//     //
//     // for(size_t i = 0; i < data->meshes_count; ++i)
//     // {
//     //     cgltf_mesh* mesh = &data->meshes[i];
//     //
//     //     for(size_t p = 0; p < mesh->primitives_count; ++p)
//     //     {
//     //         cgltf_primitive* prim = &mesh->primitives[p];
//     //
//     //         // Positions
//     //         cgltf_accessor* pos = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
//     //
//     //         float* positions = malloc(sizeof(float) * pos->count * 3);
//     //         cgltf_accessor_unpack_floats(pos, positions, pos->count * 3);
//     //
//     //         // Normals
//     //         cgltf_accessor* norm = cgltf_find_accessor(prim, cgltf_attribute_type_normal, 0);
//     //
//     //         float* normals = malloc(sizeof(float) * norm->count * 3);
//     //         cgltf_accessor_unpack_floats(norm, normals, norm->count * 3);
//     //
//     //         // UVs
//     //         cgltf_accessor* uv = cgltf_find_accessor(prim, cgltf_attribute_type_texcoord, 0);
//     //
//     //         float* uvs = malloc(sizeof(float) * uv->count * 2);
//     //         cgltf_accessor_unpack_floats(uv, uvs, uv->count * 2);
//     //
//     //         // Indices
//     //         cgltf_accessor* idx = prim->indices;
//     //
//     //         uint32_t* indices = malloc(sizeof(uint32_t) * idx->count);
//     //         cgltf_accessor_unpack_indices(idx, indices, idx->count);
//     //     }
//     // }
// }
