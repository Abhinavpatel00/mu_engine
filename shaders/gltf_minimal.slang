struct Push
{
    uint64_t pos_ptr;
    uint64_t idx_ptr;

    float4x4 model;
    float4x4 view_proj;

    float4 color;
    uint index_count;
};

struct VSOutput
{
    float4 position : SV_Position;
};

[[vk::push_constant]]
Push push;

[shader("vertex")]
VSOutput vs_main(uint vertexID : SV_VertexID)
{
    VSOutput o;

    uint* indices = (uint*)push.idx_ptr;
    float* positions = (float*)push.pos_ptr;

    uint idx = indices[vertexID];

    float3 p = float3(
        positions[idx * 3 + 0],
        positions[idx * 3 + 1],
        positions[idx * 3 + 2]
    );

    float4 world = mul(push.model, float4(p, 1.0));
    o.position = mul(push.view_proj, world);
    return o;
}

[shader("fragment")]
float4 fs_main() : SV_Target0
{
    return push.color;
}
