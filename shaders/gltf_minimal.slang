struct Push
{
    uint64_t model_table_ptr;
    uint64_t instance_ptr;
    float4x4 view_proj;
};

struct ModelMeta
{
    uint64_t pos_ptr;
    uint64_t idx_ptr;
    uint index_count;
    uint material_id;
    uint _pad0;
    uint _pad1;
};

struct InstanceData
{
    uint model_id;
    uint _pad0;
    uint _pad1;
    uint _pad2;
    float4x4 model;
    float4 color;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

[[vk::push_constant]]
Push push;

[shader("vertex")]
VSOutput vs_main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput o;

    InstanceData* instances = (InstanceData*)push.instance_ptr;
    InstanceData inst = instances[instanceID];

    ModelMeta* model_table = (ModelMeta*)push.model_table_ptr;
    ModelMeta meta = model_table[inst.model_id];

    uint* indices = (uint*)meta.idx_ptr;
    float* positions = (float*)meta.pos_ptr;

    uint idx = indices[vertexID];

    float3 p = float3(
        positions[idx * 3 + 0],
        positions[idx * 3 + 1],
        positions[idx * 3 + 2]
    );

    float4 world = mul(inst.model, float4(p, 1.0));
    o.position = mul(push.view_proj, world);
    o.color = inst.color;
    return o;
}

[shader("fragment")]
float4 fs_main(VSOutput input) : SV_Target0
{
    return input.color;
}
