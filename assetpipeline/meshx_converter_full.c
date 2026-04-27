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

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

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

typedef struct ExportMaterial
{
    char* name;
    char* base_color_tex;
    char* normal_tex;
    char* orm_tex;
    float base_color_factor[4];
    float metallic_factor;
    float roughness_factor;
    uint32_t flags;
} ExportMaterial;

typedef struct ExportSubmesh
{
    uint32_t material_index;
    uint32_t vertex_offset;
    uint32_t vertex_count;
    uint32_t index_offset;
    uint32_t index_count;
    float    bounds_min[3];
    float    bounds_max[3];
} ExportSubmesh;

typedef struct ExportMesh
{
    PackedVertex* vertices;
    uint32_t      vertex_count;
    uint32_t      vertex_capacity;

    uint32_t* indices;
    uint32_t  index_count;
    uint32_t  index_capacity;

    ExportMaterial* materials;
    uint32_t        material_count;
    uint32_t        material_capacity;

    ExportSubmesh* submeshes;
    uint32_t       submesh_count;
    uint32_t       submesh_capacity;

    float bounds_min[3];
    float bounds_max[3];
} ExportMesh;

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
        else if(*p == '\\' || *p == ':' || *p == '*' || *p == '?' || *p == '<' || *p == '>' || *p == '|')
            *p = '_';
    }
}

static void path_dirname(const char* path, char* out_dir, size_t out_size)
{
    if(!path || !out_dir || out_size == 0)
        return;

    const char* slash = strrchr(path, '/');
    if(!slash)
    {
        snprintf(out_dir, out_size, ".");
        return;
    }

    size_t len = (size_t)(slash - path);
    if(len >= out_size)
        len = out_size - 1u;

    memcpy(out_dir, path, len);
    out_dir[len] = '\0';
}

static bool join_path(const char* a, const char* b, char* out, size_t out_size)
{
    if(!a || !b || !out || out_size == 0)
        return false;

    size_t a_len = strlen(a);
    size_t b_len = strlen(b);
    bool need_sep = a_len > 0 && a[a_len - 1] != '/';
    size_t total = a_len + (need_sep ? 1u : 0u) + b_len + 1u;
    if(total > out_size)
        return false;

    memcpy(out, a, a_len);
    size_t pos = a_len;
    if(need_sep)
        out[pos++] = '/';
    memcpy(out + pos, b, b_len);
    out[pos + b_len] = '\0';
    return true;
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

static void export_mesh_free(ExportMesh* mesh)
{
    if(!mesh)
        return;

    for(uint32_t i = 0; i < mesh->material_count; ++i)
    {
        free(mesh->materials[i].name);
        free(mesh->materials[i].base_color_tex);
        free(mesh->materials[i].normal_tex);
        free(mesh->materials[i].orm_tex);
    }

    free(mesh->vertices);
    free(mesh->indices);
    free(mesh->materials);
    free(mesh->submeshes);
    memset(mesh, 0, sizeof(*mesh));
}

static bool ensure_vertex_capacity(ExportMesh* mesh, uint32_t add_count)
{
    if(mesh->vertex_count + add_count <= mesh->vertex_capacity)
        return true;

    uint32_t new_capacity = mesh->vertex_capacity ? mesh->vertex_capacity * 2u : 256u;
    while(new_capacity < mesh->vertex_count + add_count)
        new_capacity *= 2u;

    PackedVertex* verts = (PackedVertex*)realloc(mesh->vertices, (size_t)new_capacity * sizeof(PackedVertex));
    if(!verts)
        return false;

    mesh->vertices = verts;
    mesh->vertex_capacity = new_capacity;
    return true;
}

static bool ensure_index_capacity(ExportMesh* mesh, uint32_t add_count)
{
    if(mesh->index_count + add_count <= mesh->index_capacity)
        return true;

    uint32_t new_capacity = mesh->index_capacity ? mesh->index_capacity * 2u : 384u;
    while(new_capacity < mesh->index_count + add_count)
        new_capacity *= 2u;

    uint32_t* indices = (uint32_t*)realloc(mesh->indices, (size_t)new_capacity * sizeof(uint32_t));
    if(!indices)
        return false;

    mesh->indices = indices;
    mesh->index_capacity = new_capacity;
    return true;
}

static bool ensure_material_capacity(ExportMesh* mesh, uint32_t add_count)
{
    if(mesh->material_count + add_count <= mesh->material_capacity)
        return true;

    uint32_t new_capacity = mesh->material_capacity ? mesh->material_capacity * 2u : 16u;
    while(new_capacity < mesh->material_count + add_count)
        new_capacity *= 2u;

    ExportMaterial* materials = (ExportMaterial*)realloc(mesh->materials, (size_t)new_capacity * sizeof(ExportMaterial));
    if(!materials)
        return false;

    mesh->materials = materials;
    mesh->material_capacity = new_capacity;
    return true;
}

static bool ensure_submesh_capacity(ExportMesh* mesh, uint32_t add_count)
{
    if(mesh->submesh_count + add_count <= mesh->submesh_capacity)
        return true;

    uint32_t new_capacity = mesh->submesh_capacity ? mesh->submesh_capacity * 2u : 16u;
    while(new_capacity < mesh->submesh_count + add_count)
        new_capacity *= 2u;

    ExportSubmesh* submeshes = (ExportSubmesh*)realloc(mesh->submeshes, (size_t)new_capacity * sizeof(ExportSubmesh));
    if(!submeshes)
        return false;

    mesh->submeshes = submeshes;
    mesh->submesh_capacity = new_capacity;
    return true;
}

static void update_bounds(float bmin[3], float bmax[3], const float pos[3])
{
    if(pos[0] < bmin[0]) bmin[0] = pos[0];
    if(pos[1] < bmin[1]) bmin[1] = pos[1];
    if(pos[2] < bmin[2]) bmin[2] = pos[2];
    if(pos[0] > bmax[0]) bmax[0] = pos[0];
    if(pos[1] > bmax[1]) bmax[1] = pos[1];
    if(pos[2] > bmax[2]) bmax[2] = pos[2];
}

static const cgltf_accessor* find_position_accessor(const cgltf_primitive* prim)
{
    for(size_t i = 0; i < prim->attributes_count; ++i)
        if(prim->attributes[i].type == cgltf_attribute_type_position)
            return prim->attributes[i].data;
    return NULL;
}

static const cgltf_accessor* find_normal_accessor(const cgltf_primitive* prim)
{
    for(size_t i = 0; i < prim->attributes_count; ++i)
        if(prim->attributes[i].type == cgltf_attribute_type_normal)
            return prim->attributes[i].data;
    return NULL;
}

static const cgltf_accessor* find_tangent_accessor(const cgltf_primitive* prim)
{
    for(size_t i = 0; i < prim->attributes_count; ++i)
        if(prim->attributes[i].type == cgltf_attribute_type_tangent)
            return prim->attributes[i].data;
    return NULL;
}

static const cgltf_accessor* find_texcoord0_accessor(const cgltf_primitive* prim)
{
    for(size_t i = 0; i < prim->attributes_count; ++i)
        if(prim->attributes[i].type == cgltf_attribute_type_texcoord && prim->attributes[i].index == 0)
            return prim->attributes[i].data;
    return NULL;
}

static bool copy_buffer_to_file(const char* path, const void* data, size_t size)
{
    if(!path || !data || size == 0)
        return false;

    FILE* out = fopen(path, "wb");
    if(!out)
        return false;

    bool ok = fwrite(data, 1u, size, out) == size;
    fclose(out);
    return ok;
}

static bool copy_file(const char* src_path, const char* dst_path)
{
    if(!src_path || !dst_path)
        return false;

    FILE* in = fopen(src_path, "rb");
    if(!in)
        return false;

    FILE* out = fopen(dst_path, "wb");
    if(!out)
    {
        fclose(in);
        return false;
    }

    char buffer[64 * 1024];
    bool ok = true;
    size_t read_bytes;
    while((read_bytes = fread(buffer, 1u, sizeof(buffer), in)) > 0u)
    {
        if(fwrite(buffer, 1u, read_bytes, out) != read_bytes)
        {
            ok = false;
            break;
        }
    }

    if(ferror(in))
        ok = false;

    fclose(in);
    fclose(out);
    return ok;
}

static const char* extension_for_mime_type(const char* mime_type)
{
    if(!mime_type)
        return NULL;
    if(strcmp(mime_type, "image/png") == 0)
        return ".png";
    if(strcmp(mime_type, "image/jpeg") == 0)
        return ".jpg";
    if(strcmp(mime_type, "image/webp") == 0)
        return ".webp";
    if(strcmp(mime_type, "image/ktx2") == 0)
        return ".ktx2";
    return NULL;
}

static bool export_image_to_dir(const cgltf_image* image, const char* input_path, const char* output_dir, uint32_t image_index, char* out_uri, size_t out_uri_size)
{
    if(!image || !input_path || !output_dir || !out_uri || out_uri_size == 0)
        return false;

    char base_name[256] = {0};
    if(image->name && image->name[0] != '\0')
        snprintf(base_name, sizeof(base_name), "%s", image->name);
    else
        snprintf(base_name, sizeof(base_name), "image_%u", image_index);

    strip_extension(base_name);
    sanitize_ascii_token(base_name);

    const char* ext = extension_for_mime_type(image->mime_type);
    char filename[512] = {0};

    if(image->uri && image->uri[0] != '\0')
    {
        const char* uri_base = path_basename(image->uri);
        snprintf(filename, sizeof(filename), "%s", uri_base);
        sanitize_ascii_token(filename);
        if(!strchr(filename, '.'))
        {
            if(ext)
                snprintf(filename, sizeof(filename), "%s%s", base_name, ext);
            else
                snprintf(filename, sizeof(filename), "%s.bin", base_name);
        }
    }
    else
    {
        if(ext)
            snprintf(filename, sizeof(filename), "%s%s", base_name, ext);
        else
            snprintf(filename, sizeof(filename), "%s.bin", base_name);
    }

    char dst_path[PATH_MAX];
    if(!join_path(output_dir, filename, dst_path, sizeof(dst_path)))
        return false;

    bool ok = false;
    if(image->uri && image->uri[0] != '\0')
    {
        if(strstr(image->uri, "://") != NULL)
            return false;

        char source_dir[PATH_MAX];
        path_dirname(input_path, source_dir, sizeof(source_dir));

        char src_path[PATH_MAX];
        if(image->uri[0] == '/')
            snprintf(src_path, sizeof(src_path), "%s", image->uri);
        else if(!join_path(source_dir, image->uri, src_path, sizeof(src_path)))
            return false;

        ok = copy_file(src_path, dst_path);
    }
    else if(image->buffer_view && image->buffer_view->buffer && image->buffer_view->buffer->data)
    {
        const uint8_t* bytes = (const uint8_t*)image->buffer_view->buffer->data + image->buffer_view->offset;
        size_t size = image->buffer_view->size;
        ok = copy_buffer_to_file(dst_path, bytes, size);
    }

    if(!ok)
        return false;

    snprintf(out_uri, out_uri_size, "%s", filename);
    return true;
}

static bool export_all_images(const cgltf_data* data, const char* input_path, const char* output_dir, char*** out_uris)
{
    if(!data || !input_path || !output_dir || !out_uris)
        return false;

    *out_uris = NULL;
    if(data->images_count == 0)
        return true;

    char** uris = (char**)calloc(data->images_count, sizeof(char*));
    if(!uris)
        return false;

    for(size_t i = 0; i < data->images_count; ++i)
    {
        char uri[PATH_MAX] = {0};
        if(!export_image_to_dir(&data->images[i], input_path, output_dir, (uint32_t)i, uri, sizeof(uri)))
        {
            for(size_t j = 0; j < i; ++j)
                free(uris[j]);
            free(uris);
            return false;
        }

        uris[i] = strdup_owned(uri);
        if(!uris[i])
        {
            for(size_t j = 0; j < i; ++j)
                free(uris[j]);
            free(uris);
            return false;
        }
    }

    *out_uris = uris;
    return true;
}

static const char* texture_uri_for_image(const cgltf_texture* texture, const cgltf_data* data, char** image_uris)
{
    if(!texture || !texture->image || !data || !image_uris)
        return NULL;

    size_t image_index = (size_t)(texture->image - data->images);
    if(image_index >= data->images_count)
        return NULL;
    return image_uris[image_index];
}

static bool append_material(ExportMesh* mesh,
                            const cgltf_material* material,
                            const char* base_color_tex,
                            const char* normal_tex,
                            const char* orm_tex)
{
    if(!mesh || !ensure_material_capacity(mesh, 1u))
        return false;

    ExportMaterial* out = &mesh->materials[mesh->material_count];
    memset(out, 0, sizeof(*out));
    out->base_color_factor[0] = 1.0f;
    out->base_color_factor[1] = 1.0f;
    out->base_color_factor[2] = 1.0f;
    out->base_color_factor[3] = 1.0f;
    out->metallic_factor = 1.0f;
    out->roughness_factor = 1.0f;
    out->flags = 0;

    char name_buf[256] = {0};
    if(material && material->name)
        snprintf(name_buf, sizeof(name_buf), "%s", material->name);
    else
        snprintf(name_buf, sizeof(name_buf), "material_%u", mesh->material_count);
    sanitize_ascii_token(name_buf);
    out->name = strdup_owned(name_buf);
    if(!out->name)
        return false;

    if(material)
    {
        const cgltf_pbr_metallic_roughness* pbr = &material->pbr_metallic_roughness;
        out->base_color_factor[0] = pbr->base_color_factor[0];
        out->base_color_factor[1] = pbr->base_color_factor[1];
        out->base_color_factor[2] = pbr->base_color_factor[2];
        out->base_color_factor[3] = pbr->base_color_factor[3];
        out->metallic_factor = pbr->metallic_factor;
        out->roughness_factor = pbr->roughness_factor;

        if(material->alpha_mode == cgltf_alpha_mode_mask)
            out->flags |= 1u;
        else if(material->alpha_mode == cgltf_alpha_mode_blend)
            out->flags |= 2u;
        if(material->double_sided)
            out->flags |= 4u;
    }

    if(base_color_tex && base_color_tex[0] != '\0')
    {
        out->base_color_tex = strdup_owned(base_color_tex);
        if(!out->base_color_tex)
            return false;
    }
    if(normal_tex && normal_tex[0] != '\0')
    {
        out->normal_tex = strdup_owned(normal_tex);
        if(!out->normal_tex)
            return false;
    }
    if(orm_tex && orm_tex[0] != '\0')
    {
        out->orm_tex = strdup_owned(orm_tex);
        if(!out->orm_tex)
            return false;
    }

    mesh->material_count++;
    return true;
}

static bool export_materials(const cgltf_data* data, const char* input_path, const char* output_dir, ExportMesh* mesh)
{
    if(!data || !mesh)
        return false;

    char** image_uris = NULL;
    if(!export_all_images(data, input_path, output_dir, &image_uris))
        return false;

    bool ok = true;
    if(data->materials_count == 0)
    {
        ok = append_material(mesh, NULL, NULL, NULL, NULL);
    }
    else
    {
        for(size_t i = 0; i < data->materials_count && ok; ++i)
        {
            const cgltf_material* material = &data->materials[i];
            const cgltf_pbr_metallic_roughness* pbr = &material->pbr_metallic_roughness;

            const char* base_color_tex = texture_uri_for_image(pbr->base_color_texture.texture, data, image_uris);
            const char* normal_tex = texture_uri_for_image(material->normal_texture.texture, data, image_uris);
            const char* orm_tex = texture_uri_for_image(pbr->metallic_roughness_texture.texture, data, image_uris);

            ok = append_material(mesh, material, base_color_tex, normal_tex, orm_tex);
        }
    }

    if(image_uris)
    {
        for(size_t i = 0; i < data->images_count; ++i)
            free(image_uris[i]);
        free(image_uris);
    }

    return ok;
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

        vec3 e1, e2, n;
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

        vec3 e1, e2;
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
        vec3 t = {(e1[0] * dv2 - e2[0] * dv1) * inv, (e1[1] * dv2 - e2[1] * dv1) * inv, (e1[2] * dv2 - e2[2] * dv1) * inv};
        vec3 b = {(e2[0] * du1 - e1[0] * du2) * inv, (e2[1] * du1 - e1[1] * du2) * inv, (e2[2] * du1 - e1[2] * du2) * inv};

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

        vec3 ortho;
        float ndott = glm_vec3_dot(n, t);
        glm_vec3_sub(t, (vec3){n[0] * ndott, n[1] * ndott, n[2] * ndott}, ortho);
        glm_vec3_normalize(ortho);

        vec3 cross_nb;
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

static bool append_primitive_mesh(ExportMesh* mesh,
                                  const cgltf_primitive* prim,
                                  const cgltf_node* node,
                                  uint32_t material_index,
                                  const ConverterOptions* options)
{
    const cgltf_accessor* pos_accessor = find_position_accessor(prim);
    if(!pos_accessor || pos_accessor->count == 0)
        return true;

    uint32_t vtx = (uint32_t)pos_accessor->count;
    uint32_t idx = prim->indices ? (uint32_t)prim->indices->count : vtx;
    if(idx == 0)
        return true;

    PackedVertex* source_vertices = (PackedVertex*)calloc(vtx, sizeof(PackedVertex));
    uint32_t* source_indices = (uint32_t*)malloc((size_t)idx * sizeof(uint32_t));
    if(!source_vertices || !source_indices)
    {
        free(source_vertices);
        free(source_indices);
        return false;
    }

    float world[16];
    cgltf_node_transform_world(node, world);

    cgltf_size unpacked_pos = cgltf_accessor_unpack_floats(pos_accessor, &source_vertices[0].position[0], (cgltf_size)vtx * 3u);
    if(unpacked_pos < (cgltf_size)vtx * 3u)
    {
        free(source_vertices);
        free(source_indices);
        return false;
    }

    for(uint32_t i = 0; i < vtx; ++i)
    {
        float x = source_vertices[i].position[0];
        float y = source_vertices[i].position[1];
        float z = source_vertices[i].position[2];
        source_vertices[i].position[0] = x * world[0] + y * world[4] + z * world[8] + world[12];
        source_vertices[i].position[1] = x * world[1] + y * world[5] + z * world[9] + world[13];
        source_vertices[i].position[2] = x * world[2] + y * world[6] + z * world[10] + world[14];
    }

    const cgltf_accessor* uv_accessor = find_texcoord0_accessor(prim);
    if(uv_accessor)
    {
        cgltf_size unpacked_uv = cgltf_accessor_unpack_floats(uv_accessor, &source_vertices[0].uv[0], (cgltf_size)vtx * 2u);
        if(unpacked_uv < (cgltf_size)vtx * 2u)
        {
            free(source_vertices);
            free(source_indices);
            return false;
        }
    }

    const cgltf_accessor* normal_accessor = find_normal_accessor(prim);
    if(normal_accessor)
    {
        cgltf_size unpacked_n = cgltf_accessor_unpack_floats(normal_accessor, &source_vertices[0].normal[0], (cgltf_size)vtx * 3u);
        if(unpacked_n < (cgltf_size)vtx * 3u)
        {
            free(source_vertices);
            free(source_indices);
            return false;
        }
    }
    else
    {
        for(uint32_t i = 0; i < vtx; ++i)
        {
            source_vertices[i].normal[0] = 0.0f;
            source_vertices[i].normal[1] = 0.0f;
            source_vertices[i].normal[2] = 1.0f;
        }
    }

    const cgltf_accessor* tangent_accessor = find_tangent_accessor(prim);
    if(tangent_accessor)
    {
        cgltf_size unpacked_t = cgltf_accessor_unpack_floats(tangent_accessor, &source_vertices[0].tangent[0], (cgltf_size)vtx * 4u);
        if(unpacked_t < (cgltf_size)vtx * 4u)
        {
            free(source_vertices);
            free(source_indices);
            return false;
        }
    }
    else
    {
        for(uint32_t i = 0; i < vtx; ++i)
        {
            source_vertices[i].tangent[0] = 1.0f;
            source_vertices[i].tangent[1] = 0.0f;
            source_vertices[i].tangent[2] = 0.0f;
            source_vertices[i].tangent[3] = 1.0f;
        }
    }

    if(prim->indices)
    {
        cgltf_size unpacked_idx = cgltf_accessor_unpack_indices(prim->indices, source_indices, sizeof(uint32_t), idx);
        if(unpacked_idx < idx)
        {
            free(source_vertices);
            free(source_indices);
            return false;
        }
    }
    else
    {
        for(uint32_t i = 0; i < idx; ++i)
            source_indices[i] = i;
    }

    if(options->generate_normals && !normal_accessor)
        generate_normals(source_indices, idx, source_vertices, vtx);
    if(options->generate_tangents && !tangent_accessor)
        generate_tangents(source_indices, idx, source_vertices, vtx);

    unsigned int* remap = (unsigned int*)malloc((size_t)vtx * sizeof(unsigned int));
    if(!remap)
    {
        free(source_vertices);
        free(source_indices);
        return false;
    }

    size_t unique_vertices = meshopt_generateVertexRemap(remap, source_indices, idx, source_vertices, vtx, sizeof(PackedVertex));
    if(unique_vertices == 0)
    {
        free(remap);
        free(source_vertices);
        free(source_indices);
        return false;
    }

    PackedVertex* vertices = (PackedVertex*)malloc(unique_vertices * sizeof(PackedVertex));
    uint32_t* indices = (uint32_t*)malloc((size_t)idx * sizeof(uint32_t));
    if(!vertices || !indices)
    {
        free(vertices);
        free(indices);
        free(remap);
        free(source_vertices);
        free(source_indices);
        return false;
    }

    meshopt_remapVertexBuffer(vertices, source_vertices, vtx, sizeof(PackedVertex), remap);
    meshopt_remapIndexBuffer(indices, source_indices, idx, remap);

    if(options->optimize)
    {
        meshopt_optimizeVertexCache(indices, indices, idx, unique_vertices);
        unique_vertices = meshopt_optimizeVertexFetch(vertices, indices, idx, vertices, unique_vertices, sizeof(PackedVertex));
    }

    if(!ensure_vertex_capacity(mesh, (uint32_t)unique_vertices) || !ensure_index_capacity(mesh, idx) || !ensure_submesh_capacity(mesh, 1u))
    {
        free(vertices);
        free(indices);
        free(remap);
        free(source_vertices);
        free(source_indices);
        return false;
    }

    ExportSubmesh* submesh = &mesh->submeshes[mesh->submesh_count];
    memset(submesh, 0, sizeof(*submesh));
    submesh->material_index = material_index;
    submesh->vertex_offset = mesh->vertex_count;
    submesh->vertex_count = (uint32_t)unique_vertices;
    submesh->index_offset = mesh->index_count;
    submesh->index_count = idx;

    float bmin[3] = {0.0f, 0.0f, 0.0f};
    float bmax[3] = {0.0f, 0.0f, 0.0f};
    for(uint32_t i = 0; i < unique_vertices; ++i)
    {
        if(i == 0)
        {
            bmin[0] = bmax[0] = vertices[i].position[0];
            bmin[1] = bmax[1] = vertices[i].position[1];
            bmin[2] = bmax[2] = vertices[i].position[2];
        }
        else
        {
            update_bounds(bmin, bmax, vertices[i].position);
        }

        mesh->vertices[mesh->vertex_count + i] = vertices[i];
    }

    for(uint32_t i = 0; i < idx; ++i)
        mesh->indices[mesh->index_count + i] = (uint32_t)(indices[i] + mesh->vertex_count);

    submesh->bounds_min[0] = bmin[0];
    submesh->bounds_min[1] = bmin[1];
    submesh->bounds_min[2] = bmin[2];
    submesh->bounds_max[0] = bmax[0];
    submesh->bounds_max[1] = bmax[1];
    submesh->bounds_max[2] = bmax[2];

    if(mesh->submesh_count == 0)
    {
        mesh->bounds_min[0] = bmin[0];
        mesh->bounds_min[1] = bmin[1];
        mesh->bounds_min[2] = bmin[2];
        mesh->bounds_max[0] = bmax[0];
        mesh->bounds_max[1] = bmax[1];
        mesh->bounds_max[2] = bmax[2];
    }
    else
    {
        update_bounds(mesh->bounds_min, mesh->bounds_max, bmin);
        update_bounds(mesh->bounds_min, mesh->bounds_max, bmax);
    }

    mesh->vertex_count += (uint32_t)unique_vertices;
    mesh->index_count += idx;
    mesh->submesh_count++;

    free(vertices);
    free(indices);
    free(remap);
    free(source_vertices);
    free(source_indices);
    return true;
}

static bool append_node_primitives(const cgltf_node* node, ExportMesh* mesh, const cgltf_data* data, const ConverterOptions* options)
{
    if(node->mesh)
    {
        for(size_t prim_i = 0; prim_i < node->mesh->primitives_count; ++prim_i)
        {
            const cgltf_primitive* prim = &node->mesh->primitives[prim_i];
            if(prim->type != cgltf_primitive_type_triangles)
                continue;

            uint32_t material_index = 0u;
            if(prim->material && data->materials_count > 0)
                material_index = (uint32_t)(prim->material - data->materials);

            if(!append_primitive_mesh(mesh, prim, node, material_index, options))
                return false;
        }
    }

    for(size_t child_i = 0; child_i < node->children_count; ++child_i)
    {
        if(!append_node_primitives(node->children[child_i], mesh, data, options))
            return false;
    }

    return true;
}

static bool export_geometry(const cgltf_data* data, const ConverterOptions* options, ExportMesh* mesh)
{
    const cgltf_scene* scene = data->scene;
    if(!scene && data->scenes_count > 0)
        scene = &data->scenes[0];

    if(scene && scene->nodes_count > 0)
    {
        for(size_t i = 0; i < scene->nodes_count; ++i)
        {
            if(!append_node_primitives(scene->nodes[i], mesh, data, options))
                return false;
        }
        return true;
    }

    for(size_t mesh_i = 0; mesh_i < data->meshes_count; ++mesh_i)
    {
        const cgltf_mesh* cgmesh = &data->meshes[mesh_i];
        for(size_t prim_i = 0; prim_i < cgmesh->primitives_count; ++prim_i)
        {
            const cgltf_primitive* prim = &cgmesh->primitives[prim_i];
            if(prim->type != cgltf_primitive_type_triangles)
                continue;

            uint32_t material_index = 0u;
            if(prim->material && data->materials_count > 0)
                material_index = (uint32_t)(prim->material - data->materials);

            cgltf_node fake_node = {0};
            fake_node.mesh = (cgltf_mesh*)cgmesh;
            if(!append_primitive_mesh(mesh, prim, &fake_node, material_index, options))
                return false;
        }
    }

    return true;
}

static void write_escaped(FILE* out, const char* text)
{
    fputc('"', out);
    for(const char* p = text; p && *p; ++p)
    {
        if(*p == '"' || *p == '\\')
            fputc('\\', out);
        fputc(*p, out);
    }
    fputc('"', out);
}

static bool write_meshx(FILE* out, const char* input_path, const ExportMesh* mesh)
{
    if(!out || !input_path || !mesh || mesh->vertex_count == 0 || mesh->index_count == 0)
        return false;

    float bmin[3] = {mesh->bounds_min[0], mesh->bounds_min[1], mesh->bounds_min[2]};
    float bmax[3] = {mesh->bounds_max[0], mesh->bounds_max[1], mesh->bounds_max[2]};

    char mesh_name[256] = {0};
    const char* base = path_basename(input_path);
    snprintf(mesh_name, sizeof(mesh_name), "%s", base);
    strip_extension(mesh_name);
    sanitize_ascii_token(mesh_name);

    fprintf(out, "mesh \"");
    fputs(mesh_name[0] ? mesh_name : "mesh", out);
    fprintf(out, "\"\n\n");

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

    for(uint32_t i = 0; i < mesh->material_count; ++i)
    {
        const ExportMaterial* mat = &mesh->materials[i];
        fprintf(out, "material %u\n{\n", i);
        fprintf(out, "    name ");
        write_escaped(out, mat->name ? mat->name : "material");
        fprintf(out, "\n");

        fprintf(out, "    base_color_tex ");
        write_escaped(out, mat->base_color_tex && mat->base_color_tex[0] ? mat->base_color_tex : "__none__");
        fprintf(out, "\n");

        fprintf(out, "    normal_tex     ");
        write_escaped(out, mat->normal_tex && mat->normal_tex[0] ? mat->normal_tex : "__none__");
        fprintf(out, "\n");

        fprintf(out, "    orm_tex        ");
        write_escaped(out, mat->orm_tex && mat->orm_tex[0] ? mat->orm_tex : "__none__");
        fprintf(out, "\n\n");

        fprintf(out, "    base_color_factor %.9g %.9g %.9g %.9g\n",
                mat->base_color_factor[0], mat->base_color_factor[1], mat->base_color_factor[2], mat->base_color_factor[3]);
        fprintf(out, "    metallic %.9g\n", mat->metallic_factor);
        fprintf(out, "    roughness %.9g\n", mat->roughness_factor);

        if(mat->flags & 1u)
            fprintf(out, "    alpha_mode mask\n");
        else if(mat->flags & 2u)
            fprintf(out, "    alpha_mode blend\n");
        else
            fprintf(out, "    alpha_mode opaque\n");

        fprintf(out, "}\n\n");
    }

    for(uint32_t i = 0; i < mesh->submesh_count; ++i)
    {
        const ExportSubmesh* sm = &mesh->submeshes[i];
        fprintf(out, "submesh %u\n{\n", i);
        fprintf(out, "    material %u\n", sm->material_index);
        fprintf(out, "    vertex_offset %u\n", sm->vertex_offset);
        fprintf(out, "    vertex_count %u\n", sm->vertex_count);
        fprintf(out, "    index_offset %u\n", sm->index_offset);
        fprintf(out, "    index_count %u\n\n", sm->index_count);
        fprintf(out, "    bounds\n    {\n");
        fprintf(out, "        min %.9g %.9g %.9g\n", sm->bounds_min[0], sm->bounds_min[1], sm->bounds_min[2]);
        fprintf(out, "        max %.9g %.9g %.9g\n", sm->bounds_max[0], sm->bounds_max[1], sm->bounds_max[2]);
        fprintf(out, "    }\n");
        fprintf(out, "}\n\n");
    }

    fprintf(out, "vertices\n{\n");
    for(uint32_t i = 0; i < mesh->vertex_count; ++i)
    {
        fprintf(out, "    v %.9g %.9g %.9g   %.9g %.9g %.9g   %.9g %.9g   %.9g %.9g %.9g %.9g\n",
                mesh->vertices[i].position[0], mesh->vertices[i].position[1], mesh->vertices[i].position[2],
                mesh->vertices[i].normal[0], mesh->vertices[i].normal[1], mesh->vertices[i].normal[2],
                mesh->vertices[i].uv[0], mesh->vertices[i].uv[1],
                mesh->vertices[i].tangent[0], mesh->vertices[i].tangent[1], mesh->vertices[i].tangent[2], mesh->vertices[i].tangent[3]);
    }
    fprintf(out, "}\n\n");

    fprintf(out, "indices\n{\n");
    for(uint32_t i = 0; i < mesh->index_count; i += 3u)
        fprintf(out, "    i %u %u %u\n", mesh->indices[i + 0u], mesh->indices[i + 1u], mesh->indices[i + 2u]);
    fprintf(out, "}\n");

    return !ferror(out);
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

    cgltf_options cgltf_opts = {0};
    cgltf_data* data = NULL;
    if(cgltf_parse_file(&cgltf_opts, input_path, &data) != cgltf_result_success)
    {
        fprintf(stderr, "meshx_converter: failed to parse input: %s\n", input_path);
        return 1;
    }

    if(cgltf_load_buffers(&cgltf_opts, data, input_path) != cgltf_result_success)
    {
        fprintf(stderr, "meshx_converter: failed to load buffers: %s\n", input_path);
        cgltf_free(data);
        return 1;
    }

    if(cgltf_validate(data) != cgltf_result_success)
    {
        fprintf(stderr, "meshx_converter: failed to validate input: %s\n", input_path);
        cgltf_free(data);
        return 1;
    }

    char output_dir[PATH_MAX];
    path_dirname(output_path, output_dir, sizeof(output_dir));

    ExportMesh mesh = {0};
    mesh.bounds_min[0] = mesh.bounds_min[1] = mesh.bounds_min[2] = 0.0f;
    mesh.bounds_max[0] = mesh.bounds_max[1] = mesh.bounds_max[2] = 0.0f;

    if(!export_materials(data, input_path, output_dir, &mesh))
    {
        fprintf(stderr, "meshx_converter: failed to export materials: %s\n", input_path);
        export_mesh_free(&mesh);
        cgltf_free(data);
        return 1;
    }

    if(!export_geometry(data, &options, &mesh))
    {
        fprintf(stderr, "meshx_converter: failed to export geometry: %s\n", input_path);
        export_mesh_free(&mesh);
        cgltf_free(data);
        return 1;
    }

    FILE* out = fopen(output_path, "wb");
    if(!out)
    {
        fprintf(stderr, "meshx_converter: failed to open output: %s\n", output_path);
        export_mesh_free(&mesh);
        cgltf_free(data);
        return 1;
    }

    bool ok = write_meshx(out, input_path, &mesh);
    fclose(out);

    export_mesh_free(&mesh);
    cgltf_free(data);

    if(!ok)
    {
        fprintf(stderr, "meshx_converter: failed while writing meshx: %s\n", output_path);
        return 1;
    }

    fprintf(stdout, "Converted %s -> %s\n", input_path, output_path);
    return 0;
}
