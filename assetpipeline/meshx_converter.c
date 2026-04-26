#include <cglm/cglm.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CGLTF_IMPLEMENTATION
#include "../external/cgltf/cgltf.h"
#include "../external/meshoptimizer/src/meshoptimizer.h"

#include "../gltfloader_minimal.h"

typedef struct PackedVertex
{
    float position[3];
    float normal[3];
    float uv[2];
    float tangent[4];
} PackedVertex;

typedef struct ConverterOptions
{
    bool generate_normals;
    bool generate_tangents;
    bool optimize;
} ConverterOptions;

static const char* path_basename(const char* path)
{
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void strip_extension(char* name)
{
    char* dot = strrchr(name, '.');
    if(dot)
        *dot = '\0';
}

static void sanitize_ascii_token(char* s)
{
    if(!s)
        return;

    for(char* p = s; *p; ++p)
    {
        if(*p == '"')
            *p = '\'';
    }
}



static void compute_bounds(const GltfMinimalMesh* mesh, float out_min[3], float out_max[3])
{
    out_min[0] = out_min[1] = out_min[2] = 0.0f;
    out_max[0] = out_max[1] = out_max[2] = 0.0f;

    if(mesh->vertex_count == 0 || !mesh->positions_xyz)
        return;

    out_min[0] = out_max[0] = mesh->positions_xyz[0];
    out_min[1] = out_max[1] = mesh->positions_xyz[1];
    out_min[2] = out_max[2] = mesh->positions_xyz[2];

    for(uint32_t i = 1; i < mesh->vertex_count; ++i)
    {
        float x = mesh->positions_xyz[i * 3u + 0u];
        float y = mesh->positions_xyz[i * 3u + 1u];
        float z = mesh->positions_xyz[i * 3u + 2u];

        if(x < out_min[0])
            out_min[0] = x;
        if(y < out_min[1])
            out_min[1] = y;
        if(z < out_min[2])
            out_min[2] = z;

        if(x > out_max[0])
            out_max[0] = x;
        if(y > out_max[1])
            out_max[1] = y;
        if(z > out_max[2])
            out_max[2] = z;
    }
}

static bool append_vertex_data(const GltfMinimalMesh* mesh, PackedVertex* vertices)
{
    if(!mesh || !vertices)
        return false;

    for(uint32_t i = 0; i < mesh->vertex_count; ++i)
    {
        vertices[i].position[0] = mesh->positions_xyz[i * 3u + 0u];
        vertices[i].position[1] = mesh->positions_xyz[i * 3u + 1u];
        vertices[i].position[2] = mesh->positions_xyz[i * 3u + 2u];
        vertices[i].normal[0] = 0.0f;
        vertices[i].normal[1] = 0.0f;
        vertices[i].normal[2] = 1.0f;
        vertices[i].uv[0] = mesh->texcoord0_xy[i * 2u + 0u];
        vertices[i].uv[1] = mesh->texcoord0_xy[i * 2u + 1u];
        vertices[i].tangent[0] = 1.0f;
        vertices[i].tangent[1] = 0.0f;
        vertices[i].tangent[2] = 0.0f;
        vertices[i].tangent[3] = 1.0f;
    }

    return true;
}

static void generate_normals(const uint32_t* indices, uint32_t index_count, PackedVertex* vertices, uint32_t vertex_count)
{
    float* accum = (float*)calloc((size_t)vertex_count * 3u, sizeof(float));
    if(!accum)
        return;

    for(uint32_t i = 0; i + 2u < index_count; i += 3u)
    {
        uint32_t i0 = indices[i + 0u];
        uint32_t i1 = indices[i + 1u];
        uint32_t i2 = indices[i + 2u];
        if(i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count)
            continue;

        const float* p0 = vertices[i0].position;
        const float* p1 = vertices[i1].position;
        const float* p2 = vertices[i2].position;

        float e1[3], e2[3], n[3];
        glm_vec3_sub((vec3){p1[0], p1[1], p1[2]}, (vec3){p0[0], p0[1], p0[2]}, e1);
        glm_vec3_sub((vec3){p2[0], p2[1], p2[2]}, (vec3){p0[0], p0[1], p0[2]}, e2);
        glm_vec3_cross(e1, e2, n);

        glm_vec3_add(&accum[i0 * 3u], n, &accum[i0 * 3u]);
        glm_vec3_add(&accum[i1 * 3u], n, &accum[i1 * 3u]);
        glm_vec3_add(&accum[i2 * 3u], n, &accum[i2 * 3u]);
    }

    for(uint32_t i = 0; i < vertex_count; ++i)
    {
        float* n = &accum[i * 3u];
        glm_vec3_normalize(n);
        vertices[i].normal[0] = n[0];
        vertices[i].normal[1] = n[1];
        vertices[i].normal[2] = n[2];
    }

    free(accum);
}

static void generate_tangents(const uint32_t* indices, uint32_t index_count, PackedVertex* vertices, uint32_t vertex_count)
{
    float* tan1 = (float*)calloc((size_t)vertex_count * 3u, sizeof(float));
    float* tan2 = (float*)calloc((size_t)vertex_count * 3u, sizeof(float));
    if(!tan1 || !tan2)
    {
        free(tan1);
        free(tan2);
        return;
    }

    for(uint32_t i = 0; i + 2u < index_count; i += 3u)
    {
        uint32_t i0 = indices[i + 0u];
        uint32_t i1 = indices[i + 1u];
        uint32_t i2 = indices[i + 2u];
        if(i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count)
            continue;

        const float* p0 = vertices[i0].position;
        const float* p1 = vertices[i1].position;
        const float* p2 = vertices[i2].position;
        const float* w0 = vertices[i0].uv;
        const float* w1 = vertices[i1].uv;
        const float* w2 = vertices[i2].uv;

        float e1[3], e2[3];
        glm_vec3_sub((vec3){p1[0], p1[1], p1[2]}, (vec3){p0[0], p0[1], p0[2]}, e1);
        glm_vec3_sub((vec3){p2[0], p2[1], p2[2]}, (vec3){p0[0], p0[1], p0[2]}, e2);

        float du1 = w1[0] - w0[0];
        float dv1 = w1[1] - w0[1];
        float du2 = w2[0] - w0[0];
        float dv2 = w2[1] - w0[1];
        float denom = du1 * dv2 - du2 * dv1;
        if(fabsf(denom) < 1e-20f)
            continue;

        float inv = 1.0f / denom;
        float t[3] = {(e1[0] * dv2 - e2[0] * dv1) * inv, (e1[1] * dv2 - e2[1] * dv1) * inv, (e1[2] * dv2 - e2[2] * dv1) * inv};
        float b[3] = {(e2[0] * du1 - e1[0] * du2) * inv, (e2[1] * du1 - e1[1] * du2) * inv, (e2[2] * du1 - e1[2] * du2) * inv};

        glm_vec3_add(&tan1[i0 * 3u], t, &tan1[i0 * 3u]);
        glm_vec3_add(&tan1[i1 * 3u], t, &tan1[i1 * 3u]);
        glm_vec3_add(&tan1[i2 * 3u], t, &tan1[i2 * 3u]);

        glm_vec3_add(&tan2[i0 * 3u], b, &tan2[i0 * 3u]);
        glm_vec3_add(&tan2[i1 * 3u], b, &tan2[i1 * 3u]);
        glm_vec3_add(&tan2[i2 * 3u], b, &tan2[i2 * 3u]);
    }

    for(uint32_t i = 0; i < vertex_count; ++i)
    {
        float* n = vertices[i].normal;
        float* t = &tan1[i * 3u];
        float* b = &tan2[i * 3u];

        float ortho[3];
        float ndott = glm_vec3_dot(n, t);
        glm_vec3_sub(t, (vec3){n[0] * ndott, n[1] * ndott, n[2] * ndott}, ortho);
        glm_vec3_normalize(ortho);

        float cross_nb[3];
        glm_vec3_cross(n, t, cross_nb);
        float w = (glm_vec3_dot(cross_nb, b) < 0.0f) ? -1.0f : 1.0f;

        vertices[i].tangent[0] = ortho[0];
        vertices[i].tangent[1] = ortho[1];
        vertices[i].tangent[2] = ortho[2];
        vertices[i].tangent[3] = w;
    }

    free(tan1);
    free(tan2);
}

static bool write_meshx(FILE* out, const char* input_path, const char* base_color_uri, const float base_color_factor[4], const PackedVertex* vertices,
                        const uint32_t* indices, uint32_t vertex_count, uint32_t index_count)
{
    if(!out || !input_path || !vertices || !indices)
        return false;
    if(vertex_count == 0 || index_count == 0 || (index_count % 3u) != 0u)
        return false;

    float bmin[3] = {0.0f, 0.0f, 0.0f};
    float bmax[3] = {0.0f, 0.0f, 0.0f};
    for(uint32_t i = 0; i < vertex_count; ++i)
    {
        float x = vertices[i].position[0];
        float y = vertices[i].position[1];
        float z = vertices[i].position[2];
        if(i == 0)
        {
            bmin[0] = bmax[0] = x;
            bmin[1] = bmax[1] = y;
            bmin[2] = bmax[2] = z;
        }
        else
        {
            if(x < bmin[0]) bmin[0] = x;
            if(y < bmin[1]) bmin[1] = y;
            if(z < bmin[2]) bmin[2] = z;
            if(x > bmax[0]) bmax[0] = x;
            if(y > bmax[1]) bmax[1] = y;
            if(z > bmax[2]) bmax[2] = z;
        }
    }

    char mesh_name[256] = {0};
    const char* base = path_basename(input_path);
    snprintf(mesh_name, sizeof(mesh_name), "%s", base);
    strip_extension(mesh_name);
    sanitize_ascii_token(mesh_name);

    fprintf(out, "mesh \"%s\"\n\n", mesh_name[0] ? mesh_name : "mesh");

    fprintf(out, "bounds\n{\n");
    fprintf(out, "    min %.9g %.9g %.9g\n", bmin[0], bmin[1], bmin[2]);
    fprintf(out, "    max %.9g %.9g %.9g\n", bmax[0], bmax[1], bmax[2]);
    fprintf(out, "}\n\n");

    fprintf(out, "vertex_layout\n{\n");
    fprintf(out, "    position f32x3\n");
    fprintf(out, "    normal   f32x3\n");
    fprintf(out, "    uv0      f32x2\n");
    fprintf(out, "    tangent  f32x4\n");
    fprintf(out, "}\n\n");

    fprintf(out, "material 0\n{\n");
    fprintf(out, "    name \"%s_mat\"\n", mesh_name[0] ? mesh_name : "mesh");
    if(base_color_uri && base_color_uri[0] != '\0')
        fprintf(out, "    base_color_tex \"%s\"\n", base_color_uri);
    else
        fprintf(out, "    base_color_tex \"__none__\"\n");
    fprintf(out, "    normal_tex     \"__none__\"\n");
    fprintf(out, "    orm_tex        \"__none__\"\n\n");
    fprintf(out, "    base_color_factor %.9g %.9g %.9g %.9g\n", base_color_factor ? base_color_factor[0] : 1.0f,
            base_color_factor ? base_color_factor[1] : 1.0f, base_color_factor ? base_color_factor[2] : 1.0f,
            base_color_factor ? base_color_factor[3] : 1.0f);
    fprintf(out, "    metallic 1\n");
    fprintf(out, "    roughness 1\n");
    fprintf(out, "    alpha_mode opaque\n");
    fprintf(out, "}\n\n");

    fprintf(out, "submesh 0\n{\n");
    fprintf(out, "    material 0\n");
    fprintf(out, "    vertex_offset 0\n");
    fprintf(out, "    vertex_count %u\n", vertex_count);
    fprintf(out, "    index_offset 0\n");
    fprintf(out, "    index_count %u\n\n", index_count);
    fprintf(out, "    bounds\n    {\n");
    fprintf(out, "        min %.9g %.9g %.9g\n", bmin[0], bmin[1], bmin[2]);
    fprintf(out, "        max %.9g %.9g %.9g\n", bmax[0], bmax[1], bmax[2]);
    fprintf(out, "    }\n");
    fprintf(out, "}\n\n");

    fprintf(out, "vertices\n{\n");
    for(uint32_t i = 0; i < vertex_count; ++i)
    {
        fprintf(out, "    v %.9g %.9g %.9g   %.9g %.9g %.9g   %.9g %.9g   %.9g %.9g %.9g %.9g\n",
                vertices[i].position[0], vertices[i].position[1], vertices[i].position[2],
                vertices[i].normal[0], vertices[i].normal[1], vertices[i].normal[2],
                vertices[i].uv[0], vertices[i].uv[1],
                vertices[i].tangent[0], vertices[i].tangent[1], vertices[i].tangent[2], vertices[i].tangent[3]);
    }
    fprintf(out, "}\n\n");

    fprintf(out, "indices\n{\n");
    for(uint32_t i = 0; i < index_count; i += 3u)
        fprintf(out, "    i %u %u %u\n", indices[i + 0u], indices[i + 1u], indices[i + 2u]);
    fprintf(out, "}\n");

    return !ferror(out);
}

static bool process_mesh(const char* input_path, const GltfMinimalMesh* mesh, const ConverterOptions* options, PackedVertex** out_vertices,
                         uint32_t** out_indices, uint32_t* out_vertex_count, uint32_t* out_index_count)
{
    if(!mesh || !options || !out_vertices || !out_indices || !out_vertex_count || !out_index_count)
        return false;
    if(mesh->vertex_count == 0 || mesh->index_count == 0 || !mesh->positions_xyz || !mesh->texcoord0_xy || !mesh->indices)
        return false;
    if((mesh->index_count % 3u) != 0u)
        return false;

    PackedVertex* source_vertices = (PackedVertex*)calloc(mesh->vertex_count, sizeof(PackedVertex));
    uint32_t* source_indices = (uint32_t*)malloc((size_t)mesh->index_count * sizeof(uint32_t));
    if(!source_vertices || !source_indices)
    {
        free(source_vertices);
        free(source_indices);
        return false;
    }

    if(!append_vertex_data(mesh, source_vertices))
    {
        free(source_vertices);
        free(source_indices);
        return false;
    }

    memcpy(source_indices, mesh->indices, (size_t)mesh->index_count * sizeof(uint32_t));

    if(options->generate_normals)
        generate_normals(source_indices, mesh->index_count, source_vertices, mesh->vertex_count);
    if(options->generate_tangents)
        generate_tangents(source_indices, mesh->index_count, source_vertices, mesh->vertex_count);

    unsigned int* remap = (unsigned int*)malloc((size_t)mesh->vertex_count * sizeof(unsigned int));
    if(!remap)
    {
        free(source_vertices);
        free(source_indices);
        return false;
    }

    size_t unique_vertices = meshopt_generateVertexRemap(remap, source_indices, mesh->index_count, source_vertices, mesh->vertex_count, sizeof(PackedVertex));
    if(unique_vertices == 0)
    {
        free(remap);
        free(source_vertices);
        free(source_indices);
        return false;
    }

    PackedVertex* vertices = (PackedVertex*)malloc(unique_vertices * sizeof(PackedVertex));
    uint32_t* indices = (uint32_t*)malloc((size_t)mesh->index_count * sizeof(uint32_t));
    if(!vertices || !indices)
    {
        free(vertices);
        free(indices);
        free(remap);
        free(source_vertices);
        free(source_indices);
        return false;
    }

    meshopt_remapVertexBuffer(vertices, source_vertices, mesh->vertex_count, sizeof(PackedVertex), remap);
    meshopt_remapIndexBuffer(indices, source_indices, mesh->index_count, remap);

    if(options->optimize)
    {
        meshopt_optimizeVertexCache(indices, indices, mesh->index_count, unique_vertices);
        unique_vertices = meshopt_optimizeVertexFetch(vertices, indices, mesh->index_count, vertices, unique_vertices, sizeof(PackedVertex));
    }

    free(remap);
    free(source_vertices);
    free(source_indices);

    *out_vertices = vertices;
    *out_indices = indices;
    *out_vertex_count = (uint32_t)unique_vertices;
    *out_index_count = mesh->index_count;
    (void)input_path;
    return true;
}

static void print_usage(const char* argv0)
{
    fprintf(stderr,
            "Usage: %s [--generate-normals] [--generate-tangents] [--no-optimize] <input.gltf|input.glb> <output.meshx>\n",
            argv0);
}

int main(int argc, char** argv)
{
    ConverterOptions options = {
        .generate_normals = true,
        .generate_tangents = true,
        .optimize = true,
    };

    const char* input_path = NULL;
    const char* output_path = NULL;

    for(int i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "--generate-normals") == 0)
        {
            options.generate_normals = true;
            continue;
        }
        if(strcmp(argv[i], "--generate-tangents") == 0)
        {
            options.generate_tangents = true;
            continue;
        }
        if(strcmp(argv[i], "--no-optimize") == 0)
        {
            options.optimize = false;
            continue;
        }
        if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }

        if(!input_path)
        {
            input_path = argv[i];
            continue;
        }
        if(!output_path)
        {
            output_path = argv[i];
            continue;
        }

        print_usage(argv[0]);
        return 2;
    }

    if(!input_path || !output_path)
    {
        print_usage(argv[0]);
        return 2;
    }

    GltfMinimalMesh mesh = {0};
    if(!gltf_minimal_load_first_mesh(input_path, &mesh))
    {
        fprintf(stderr, "meshx_converter: failed to parse or normalize input: %s\n", input_path);
        return 1;
    }

    PackedVertex* vertices = NULL;
    uint32_t* indices = NULL;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    if(!process_mesh(input_path, &mesh, &options, &vertices, &indices, &vertex_count, &index_count))
    {
        fprintf(stderr, "meshx_converter: failed to optimize mesh: %s\n", input_path);
        gltf_minimal_free_mesh(&mesh);
        return 1;
    }

    FILE* out = fopen(output_path, "wb");
    if(!out)
    {
        fprintf(stderr, "meshx_converter: failed to open output: %s\n", output_path);
        free(vertices);
        free(indices);
        gltf_minimal_free_mesh(&mesh);
        return 1;
    }

    bool ok = write_meshx(out, input_path, mesh.base_color_uri, mesh.base_color_factor, vertices, indices, vertex_count, index_count);
    fclose(out);

    free(vertices);
    free(indices);
    gltf_minimal_free_mesh(&mesh);

    if(!ok)
    {
        fprintf(stderr, "meshx_converter: failed while writing meshx: %s\n", output_path);
        return 1;
    }

    fprintf(stdout, "Converted %s -> %s\n", input_path, output_path);
    return 0;
}
