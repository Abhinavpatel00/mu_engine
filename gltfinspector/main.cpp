
#define CGLTF_IMPLEMENTATION

#include "../external/cgltf/cgltf.h"


/*
    gltf_inspector.c
    ----------------
    A brutally honest GLTF inspector using cgltf.

    Build:
        cc gltf_inspector.c -o gltf_inspector

    Usage:
        ./gltf_inspector model.glb

    Dependencies:
        - cgltf.h
        - stb_image.h (optional later if you want texture decode checks)

    ----------------------------------------------------------------------
    VISUAL FLOW
    ----------------------------------------------------------------------

        [ file path ]
             |
             v
      +--------------+
      | cgltf_parse  |   <- parse JSON / GLB container
      +--------------+
             |
             v
      +--------------+
      | load_buffers |   <- pull binary blob into memory
      +--------------+
             |
             v
      +--------------+
      |  validate    |   <- catch schema stupidity early
      +--------------+
             |
             v
      +-----------------------------+
      | inspector passes            |
      |-----------------------------|
      | asset      : metadata       |
      | scenes     : hierarchy      |
      | meshes     : geometry       |
      | materials  : shading        |
      | textures   : images/samplers|
      | skins      : rigging        |
      | anims      : animation      |
      | ext        : extensions     |
      +-----------------------------+

    This tool does not render.
    It exposes lies.
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* attrib_type_str(cgltf_attribute_type type)
{
    switch(type)
    {
        case cgltf_attribute_type_position:
            return "POSITION";
        case cgltf_attribute_type_normal:
            return "NORMAL";
        case cgltf_attribute_type_tangent:
            return "TANGENT";
        case cgltf_attribute_type_texcoord:
            return "TEXCOORD";
        case cgltf_attribute_type_color:
            return "COLOR";
        case cgltf_attribute_type_joints:
            return "JOINTS";
        case cgltf_attribute_type_weights:
            return "WEIGHTS";
        default:
            return "UNKNOWN";
    }
}

static const char* primitive_type_str(cgltf_primitive_type type)
{
    switch(type)
    {
        case cgltf_primitive_type_points:
            return "POINTS";
        case cgltf_primitive_type_lines:
            return "LINES";
        case cgltf_primitive_type_line_loop:
            return "LINE_LOOP";
        case cgltf_primitive_type_line_strip:
            return "LINE_STRIP";
        case cgltf_primitive_type_triangles:
            return "TRIANGLES";
        case cgltf_primitive_type_triangle_strip:
            return "TRIANGLE_STRIP";
        case cgltf_primitive_type_triangle_fan:
            return "TRIANGLE_FAN";
        default:
            return "UNKNOWN";
    }
}

static void print_indent(int depth)
{
    for(int i = 0; i < depth; ++i)
        printf("  ");
}

static void inspect_node(cgltf_node* node, int depth)
{
    print_indent(depth);
    printf("- Node: %s\n", node->name ? node->name : "<unnamed>");

    if(node->mesh)
    {
        print_indent(depth + 1);
        printf("mesh: %s\n", node->mesh->name ? node->mesh->name : "<unnamed>");
    }

    if(node->skin)
    {
        print_indent(depth + 1);
        printf("skin: yes\n");
    }

    for(cgltf_size i = 0; i < node->children_count; ++i)
        inspect_node(node->children[i], depth + 1);
}

static void inspect_asset(cgltf_data* data)
{
    printf("\n== Asset ==\n");
    printf("generator : %s\n", data->asset.generator ? data->asset.generator : "<none>");
    printf("version   : %s\n", data->asset.version ? data->asset.version : "<none>");
    printf("copyright : %s\n", data->asset.copyright ? data->asset.copyright : "<none>");
}

static void inspect_scenes(cgltf_data* data)
{
    printf("\n== Scenes ==\n");
    printf("scene_count : %zu\n", data->scenes_count);

    for(cgltf_size i = 0; i < data->scenes_count; ++i)
    {
        cgltf_scene* scene = &data->scenes[i];
        printf("[%zu] Scene: %s%s\n", i, scene->name ? scene->name : "<unnamed>", (data->scene == scene) ? " (default)" : "");

        for(cgltf_size j = 0; j < scene->nodes_count; ++j)
            inspect_node(scene->nodes[j], 1);
    }
}

static void inspect_meshes(cgltf_data* data)
{
    printf("\n== Meshes ==\n");
    printf("mesh_count : %zu\n", data->meshes_count);

    for(cgltf_size i = 0; i < data->meshes_count; ++i)
    {
        cgltf_mesh* mesh = &data->meshes[i];
        printf("\n[%zu] Mesh: %s\n", i, mesh->name ? mesh->name : "<unnamed>");
        printf("primitive_count: %zu\n", mesh->primitives_count);

        for(cgltf_size p = 0; p < mesh->primitives_count; ++p)
        {
            cgltf_primitive* prim = &mesh->primitives[p];

            int has_pos = 0, has_nrm = 0, has_tan = 0, has_uv0 = 0, has_uv1 = 0;
            int has_col = 0, has_joints = 0, has_weights = 0;

            printf("  Primitive[%zu]\n", p);
            printf("    type     : %s\n", primitive_type_str(prim->type));
            printf("    indexed  : %s\n", prim->indices ? "yes" : "no");
            printf("    material : %s\n", prim->material && prim->material->name ? prim->material->name : "<none>");

            for(cgltf_size a = 0; a < prim->attributes_count; ++a)
            {
                cgltf_attribute* attr = &prim->attributes[a];

                switch(attr->type)
                {
                    case cgltf_attribute_type_position:
                        has_pos = 1;
                        break;
                    case cgltf_attribute_type_normal:
                        has_nrm = 1;
                        break;
                    case cgltf_attribute_type_tangent:
                        has_tan = 1;
                        break;
                    case cgltf_attribute_type_texcoord:
                        if(attr->index == 0)
                            has_uv0 = 1;
                        if(attr->index == 1)
                            has_uv1 = 1;
                        break;
                    case cgltf_attribute_type_color:
                        has_col = 1;
                        break;
                    case cgltf_attribute_type_joints:
                        has_joints = 1;
                        break;
                    case cgltf_attribute_type_weights:
                        has_weights = 1;
                        break;
                    default:
                        break;
                }

                printf("    attr[%zu]  : %s_%d\n", a, attrib_type_str(attr->type), attr->index);
            }

            printf("    summary   : %s%s%s%s%s%s%s%s\n", has_pos ? "[POS]" : "[NO_POS]", has_nrm ? "[NRM]" : "[NO_NRM]",
                   has_tan ? "[TAN]" : "[NO_TAN]", has_uv0 ? "[UV0]" : "[NO_UV0]", has_uv1 ? "[UV1]" : "",
                   has_col ? "[COL]" : "", has_joints ? "[SKIN_J]" : "", has_weights ? "[SKIN_W]" : "");

            printf("\n");
        }
    }
}

static cgltf_size texture_index_from_ptr(cgltf_data* data, const cgltf_texture* tex)
{
    if(!data || !tex || !data->textures || data->textures_count == 0)
        return (cgltf_size)-1;

    if(tex < data->textures || tex >= (data->textures + data->textures_count))
        return (cgltf_size)-1;

    return (cgltf_size)(tex - data->textures);
}

static void print_texture_slot(cgltf_data* data, const char* slot_name, const cgltf_texture* tex)
{
    if(!tex)
    {
        printf("  %-18s: <none>\n", slot_name);
        return;
    }

    cgltf_size idx = texture_index_from_ptr(data, tex);
    if(idx != (cgltf_size)-1)
        printf("  %-18s: [%zu] %s\n", slot_name, idx, tex->name ? tex->name : "<unnamed>");
    else
        printf("  %-18s: %s\n", slot_name, tex->name ? tex->name : "<unnamed>");
}

static void inspect_materials(cgltf_data* data)
{
    printf("\n== Materials ==\n");
    printf("material_count : %zu\n", data->materials_count);

    for(cgltf_size i = 0; i < data->materials_count; ++i)
    {
        cgltf_material* m = &data->materials[i];
        printf("[%zu] %s\n", i, m->name ? m->name : "<unnamed>");
        printf("  metallic: %.3f\n", m->pbr_metallic_roughness.metallic_factor);
        printf("  roughness: %.3f\n", m->pbr_metallic_roughness.roughness_factor);
        printf("  double_sided: %s\n", m->double_sided ? "yes" : "no");
        printf("  alpha_mode: %d\n", (int)m->alpha_mode);
        print_texture_slot(data, "base_color", m->pbr_metallic_roughness.base_color_texture.texture);
        print_texture_slot(data, "metallic_roughness", m->pbr_metallic_roughness.metallic_roughness_texture.texture);
        print_texture_slot(data, "normal", m->normal_texture.texture);
        print_texture_slot(data, "occlusion", m->occlusion_texture.texture);
        print_texture_slot(data, "emissive", m->emissive_texture.texture);
    }
}

static void inspect_animations(cgltf_data* data)
{
    printf("\n== Animations ==\n");
    printf("animation_count : %zu\n", data->animations_count);

    for(cgltf_size i = 0; i < data->animations_count; ++i)
    {
        cgltf_animation* a = &data->animations[i];
        printf("[%zu] %s | channels=%zu samplers=%zu\n", i, a->name ? a->name : "<unnamed>", a->channels_count, a->samplers_count);
    }
}

static void inspect_extensions(cgltf_data* data)
{
    printf("\n== Extensions ==\n");
    printf("extensions_used_count     : %zu\n", data->extensions_used_count);
    printf("extensions_required_count : %zu\n", data->extensions_required_count);

    for(cgltf_size i = 0; i < data->extensions_used_count; ++i)
        printf("used     : %s\n", data->extensions_used[i]);

    for(cgltf_size i = 0; i < data->extensions_required_count; ++i)
        printf("required : %s\n", data->extensions_required[i]);
}

int main(int argc, char** argv)
{
    if(argc < 2)
    {
        fprintf(stderr, "usage: %s <file.gltf|file.glb>\n", argv[0]);
        return 1;
    }

    const char* path = argv[1];

    cgltf_options options{};
    cgltf_data*   data = NULL;

    cgltf_result result = cgltf_parse_file(&options, path, &data);
    if(result != cgltf_result_success)
    {
        fprintf(stderr, "failed to parse: %s\n", path);
        return 1;
    }

    printf("Texture Count %zu\n", data->textures_count);

    result = cgltf_load_buffers(&options, data, path);
    if(result != cgltf_result_success)
    {
        fprintf(stderr, "failed to load buffers\n");
        cgltf_free(data);
        return 1;
    }

    result = cgltf_validate(data);
    if(result != cgltf_result_success)
    {
        fprintf(stderr, "validation failed (file is technically garbage)\n");
    }

    inspect_asset(data);
    inspect_scenes(data);
    inspect_meshes(data);
    inspect_materials(data);
    inspect_animations(data);
    inspect_extensions(data);

    cgltf_free(data);
    return 0;
}
