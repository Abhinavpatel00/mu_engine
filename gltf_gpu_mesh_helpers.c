// Helper functions for skinning and packed vertex generation
// This file is included by gltf_gpu_mesh.c and has access to its internal types

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool compute_normals(ModelSource* src)
{
    if(!src || !src->positions_xyz || !src->indices)
        return false;

    if(!src->normals_xyz)
    {
        src->normals_xyz = (float*)calloc((size_t)src->vertex_count * 3u, sizeof(float));
        if(!src->normals_xyz)
            return false;
    }

    for(uint32_t i = 0; i < src->vertex_count * 3u; ++i)
        src->normals_xyz[i] = 0.0f;

    for(uint32_t i = 0; i + 2 < src->index_count; i += 3)
    {
        uint32_t a = src->indices[i + 0];
        uint32_t b = src->indices[i + 1];
        uint32_t c = src->indices[i + 2];

        float ax = src->positions_xyz[a * 3 + 0];
        float ay = src->positions_xyz[a * 3 + 1];
        float az = src->positions_xyz[a * 3 + 2];
        float bx = src->positions_xyz[b * 3 + 0];
        float by = src->positions_xyz[b * 3 + 1];
        float bz = src->positions_xyz[b * 3 + 2];
        float cx = src->positions_xyz[c * 3 + 0];
        float cy = src->positions_xyz[c * 3 + 1];
        float cz = src->positions_xyz[c * 3 + 2];

        float ux = bx - ax; float uy = by - ay; float uz = bz - az;
        float vx = cx - ax; float vy = cy - ay; float vz = cz - az;

        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;

        src->normals_xyz[a * 3 + 0] += nx;
        src->normals_xyz[a * 3 + 1] += ny;
        src->normals_xyz[a * 3 + 2] += nz;
        src->normals_xyz[b * 3 + 0] += nx;
        src->normals_xyz[b * 3 + 1] += ny;
        src->normals_xyz[b * 3 + 2] += nz;
        src->normals_xyz[c * 3 + 0] += nx;
        src->normals_xyz[c * 3 + 1] += ny;
        src->normals_xyz[c * 3 + 2] += nz;
    }

    for(uint32_t v = 0; v < src->vertex_count; ++v)
    {
        float nx = src->normals_xyz[v * 3 + 0];
        float ny = src->normals_xyz[v * 3 + 1];
        float nz = src->normals_xyz[v * 3 + 2];
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if(len > 1e-6f)
        {
            src->normals_xyz[v * 3 + 0] = nx / len;
            src->normals_xyz[v * 3 + 1] = ny / len;
            src->normals_xyz[v * 3 + 2] = nz / len;
        }
        else
        {
            src->normals_xyz[v * 3 + 0] = 0.0f;
            src->normals_xyz[v * 3 + 1] = 1.0f;
            src->normals_xyz[v * 3 + 2] = 0.0f;
        }
    }
    return true;
}

static bool compute_tangents(ModelSource* src)
{
    if(!src || !src->positions_xyz || !src->uv0_xy || !src->indices)
        return false;

    if(!src->tangents_xyzw)
    {
        src->tangents_xyzw = (float*)calloc((size_t)src->vertex_count * 4u, sizeof(float));
        if(!src->tangents_xyzw)
            return false;
    }

    float* tan1 = (float*)calloc((size_t)src->vertex_count * 3u, sizeof(float));
    if(!tan1)
        return false;

    for(uint32_t i = 0; i + 2 < src->index_count; i += 3)
    {
        uint32_t i1 = src->indices[i + 0];
        uint32_t i2 = src->indices[i + 1];
        uint32_t i3 = src->indices[i + 2];

        float x1 = src->positions_xyz[i2 * 3 + 0] - src->positions_xyz[i1 * 3 + 0];
        float y1 = src->positions_xyz[i2 * 3 + 1] - src->positions_xyz[i1 * 3 + 1];
        float z1 = src->positions_xyz[i2 * 3 + 2] - src->positions_xyz[i1 * 3 + 2];

        float x2 = src->positions_xyz[i3 * 3 + 0] - src->positions_xyz[i1 * 3 + 0];
        float y2 = src->positions_xyz[i3 * 3 + 1] - src->positions_xyz[i1 * 3 + 1];
        float z2 = src->positions_xyz[i3 * 3 + 2] - src->positions_xyz[i1 * 3 + 2];

        float s1 = src->uv0_xy[i2 * 2 + 0] - src->uv0_xy[i1 * 2 + 0];
        float t1 = src->uv0_xy[i2 * 2 + 1] - src->uv0_xy[i1 * 2 + 1];
        float s2 = src->uv0_xy[i3 * 2 + 0] - src->uv0_xy[i1 * 2 + 0];
        float t2 = src->uv0_xy[i3 * 2 + 1] - src->uv0_xy[i1 * 2 + 1];

        float r = (s1 * t2 - s2 * t1);
        if(fabsf(r) < 1e-8f)
            continue;
        float invR = 1.0f / r;

        float tx = (t2 * x1 - t1 * x2) * invR;
        float ty = (t2 * y1 - t1 * y2) * invR;
        float tz = (t2 * z1 - t1 * z2) * invR;

        tan1[i1 * 3 + 0] += tx; tan1[i1 * 3 + 1] += ty; tan1[i1 * 3 + 2] += tz;
        tan1[i2 * 3 + 0] += tx; tan1[i2 * 3 + 1] += ty; tan1[i2 * 3 + 2] += tz;
        tan1[i3 * 3 + 0] += tx; tan1[i3 * 3 + 1] += ty; tan1[i3 * 3 + 2] += tz;
    }

    for(uint32_t i = 0; i < src->vertex_count; ++i)
    {
        float tx = tan1[i * 3 + 0];
        float ty = tan1[i * 3 + 1];
        float tz = tan1[i * 3 + 2];
        float len = sqrtf(tx * tx + ty * ty + tz * tz);
        if(len > 1e-6f)
        {
            src->tangents_xyzw[i * 4 + 0] = tx / len;
            src->tangents_xyzw[i * 4 + 1] = ty / len;
            src->tangents_xyzw[i * 4 + 2] = tz / len;
            src->tangents_xyzw[i * 4 + 3] = 1.0f;
        }
        else
        {
            src->tangents_xyzw[i * 4 + 0] = 1.0f;
            src->tangents_xyzw[i * 4 + 1] = 0.0f;
            src->tangents_xyzw[i * 4 + 2] = 0.0f;
            src->tangents_xyzw[i * 4 + 3] = 1.0f;
        }
    }

    free(tan1);
    return true;
}

// Pack a packed SkinVertex stream for all vertices from offset to offset+count
// Layout: float3 pos, float3 normal, float4 tangent, uint16x4 joints, uint16x4 weights (40 bytes per vertex)
static bool pack_skin_vertices(
    ModelSource* src,
    uint32_t vertex_offset,
    uint32_t vertex_count,
    uint8_t* out_packed)
{
    if(!src || !out_packed)
        return false;

    for(uint32_t i = 0; i < vertex_count; ++i)
    {
        uint32_t vi = vertex_offset + i;
        uint8_t* dst = out_packed + i * 40u;

        // position (3 floats = 12 bytes)
        float* pos = (float*)dst;
        pos[0] = src->positions_xyz[vi * 3 + 0];
        pos[1] = src->positions_xyz[vi * 3 + 1];
        pos[2] = src->positions_xyz[vi * 3 + 2];

        // normal (3 floats = 12 bytes)
        float* norm = (float*)(dst + 12);
        norm[0] = src->normals_xyz ? src->normals_xyz[vi * 3 + 0] : 0.0f;
        norm[1] = src->normals_xyz ? src->normals_xyz[vi * 3 + 1] : 0.0f;
        norm[2] = src->normals_xyz ? src->normals_xyz[vi * 3 + 2] : 1.0f;

        // tangent (4 floats = 16 bytes)
        float* tang = (float*)(dst + 24);
        tang[0] = src->tangents_xyzw ? src->tangents_xyzw[vi * 4 + 0] : 1.0f;
        tang[1] = src->tangents_xyzw ? src->tangents_xyzw[vi * 4 + 1] : 0.0f;
        tang[2] = src->tangents_xyzw ? src->tangents_xyzw[vi * 4 + 2] : 0.0f;
        tang[3] = src->tangents_xyzw ? src->tangents_xyzw[vi * 4 + 3] : 1.0f;

        // joints (4 uint16 = 8 bytes)
        uint16_t* joints = (uint16_t*)(dst + 40);
        joints[0] = src->joints_u16 ? src->joints_u16[vi * 4 + 0] : 0;
        joints[1] = src->joints_u16 ? src->joints_u16[vi * 4 + 1] : 0;
        joints[2] = src->joints_u16 ? src->joints_u16[vi * 4 + 2] : 0;
        joints[3] = src->joints_u16 ? src->joints_u16[vi * 4 + 3] : 0;

        // weights (4 uint16 = 8 bytes) -- note offset error corrected to 48
        uint16_t* weights = (uint16_t*)(dst + 48);
        weights[0] = src->weights_u16 ? src->weights_u16[vi * 4 + 0] : 65535;
        weights[1] = src->weights_u16 ? src->weights_u16[vi * 4 + 1] : 0;
        weights[2] = src->weights_u16 ? src->weights_u16[vi * 4 + 2] : 0;
        weights[3] = src->weights_u16 ? src->weights_u16[vi * 4 + 3] : 0;
    }
    return true;
}
