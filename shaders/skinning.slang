// Compute shader for linear blend skinning
// Reads source vertices and palette matrices, outputs skinned vertices

#include "slangtypes.h"

struct SkinningPush
{
    uint64_t src_vertex_ptr;
    uint64_t dst_vertex_ptr;
    uint64_t palette_ptr;
    uint32_t vertex_count;
    uint32_t joint_count;
    uint32_t _pad0;
    uint32_t _pad1;
};

[[vk::push_constant]] SkinningPush push;

struct SkinVertex
{
    float3 position;
    float3 normal;
    float4 tangent;
    uint16_t joints[4];
    uint16_t weights[4];
};

struct SkinnedVertex
{
    float3 position;
    float3 normal;
    float3 tangent;
};

[numthreads(64, 1, 1)]
void cs_main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint global_id = dispatch_id.x;
    if (global_id >= push.vertex_count)
        return;
    
    // Cast pointers from device addresses
    SkinVertex* src_vertices = (SkinVertex*)push.src_vertex_ptr;
    float4x4* palette = (float4x4*)push.palette_ptr;
    SkinnedVertex* dst_vertices = (SkinnedVertex*)push.dst_vertex_ptr;
    
    SkinVertex src = src_vertices[global_id];
    
    float4 pos_h = {src.position, 1.0f};
    float3 norm = src.normal;
    float3 tang = src.tangent.xyz;
    
    float4 skinned_pos = {0.0f, 0.0f, 0.0f, 0.0f};
    float3 skinned_norm = {0.0f, 0.0f, 0.0f};
    float3 skinned_tang = {0.0f, 0.0f, 0.0f};
    
    for (uint i = 0; i < 4; ++i)
    {
        uint joint_idx = src.joints[i];
        float weight = float(src.weights[i]) / 65535.0f;
        
        if (weight < 0.001f || joint_idx >= push.joint_count)
            continue;
        
        float4x4 mat = palette[joint_idx];
        
        float4 transformed_pos = mul(mat, pos_h);
        skinned_pos += weight * transformed_pos;
        
        float3 transformed_norm = mul((float3x3)mat, norm);
        skinned_norm += weight * transformed_norm;
        
        float3 transformed_tang = mul((float3x3)mat, tang);
        skinned_tang += weight * transformed_tang;
    }
    
    SkinnedVertex dst;
    dst.position = skinned_pos.xyz;
    dst.normal = normalize(skinned_norm);
    dst.tangent = normalize(skinned_tang);
    
    dst_vertices[global_id] = dst;
}
