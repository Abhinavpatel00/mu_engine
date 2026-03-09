#ifndef SLANG_TYPES_H
#define SLANG_TYPES_H

/* =========================================================
   C HOST SIDE (cglm)
   ========================================================= */

#if defined(__STDC__)

#include <stdint.h>
#include <cglm/cglm.h>

/* ----- scalar types ----- */

typedef uint32_t uint;

/* ----- vector types ----- */

typedef vec2 float2;
typedef vec3 float3;
typedef vec4 float4;

typedef ivec2 int2;
typedef ivec3 int3;
typedef ivec4 int4;

typedef uvec2 uint2;
typedef uvec3 uint3;
typedef uvec4 uint4;

/* ----- matrix types ----- */

typedef mat4 float4x4;
typedef mat3 float3x3;
typedef mat2 float2x2;

/* Slang supports these but cglm doesn't really care */
typedef float float4x3[4][3];
typedef float float3x4[3][4];
typedef float float2x3[2][3];
typedef float float3x2[3][2];

/* ----- helper functions ----- */

static inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static inline void mul_mat4(mat4 a, mat4 b, mat4 out)
{
    glm_mat4_mul(a, b, out);
}

static inline void mul_mat4_vec4(mat4 m, vec4 v, vec4 out)
{
    glm_mat4_mulv(m, v, out);
}

#define SLANG_DEFAULT(x)

#ifndef NVSHADERS_OUT_TYPE
#define NVSHADERS_OUT_TYPE(T) T*
#endif

#ifndef NVSHADERS_INOUT_TYPE
#define NVSHADERS_INOUT_TYPE(T) T*
#endif

/* =========================================================
   SLANG SHADER SIDE
   ========================================================= */

#elif defined(__SLANG__)

#define float4x4 float4x4
#define float3x3 float3x3
#define float2x2 float2x2

#define float2 float2
#define float3 float3
#define float4 float4

#define int2 int2
#define int3 int3
#define int4 int4

#define uint2 uint2
#define uint3 uint3
#define uint4 uint4

#define SLANG_DEFAULT(x) = (x)

#ifndef NVSHADERS_OUT_TYPE
#define NVSHADERS_OUT_TYPE(T) out T
#endif

#ifndef NVSHADERS_INOUT_TYPE
#define NVSHADERS_INOUT_TYPE(T) inout T
#endif

#else
#error "Unknown language environment"
#endif

#endif
