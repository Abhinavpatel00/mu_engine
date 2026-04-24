struct Push
{
    uint64_t model_table_ptr;
    uint64_t material_table_ptr;
    uint64_t instance_ptr;
    uint64_t _pad0;
    float4x4 view_proj;
};

struct ModelMeta
{
    uint64_t pos_ptr;
    uint64_t uv_ptr;
    uint64_t idx_ptr;
    uint index_count;
    uint material_id;
    uint _pad0;
    uint _pad1;
};

struct MaterialData
{
    uint base_color_texture_id;
    uint sampler_id;
    uint _pad0;
    uint _pad1;
    float4 base_color_factor;
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
    float2 uv : TEXCOORD0;
    nointerpolation uint material_id : TEXCOORD1;
    float4 color : COLOR0;
};

[[vk::push_constant]]
Push push;

[[vk::binding(0, 0)]]
Texture2D textures[];

[[vk::binding(1, 0)]]
SamplerState samplers[];

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
    float* uvs = (float*)meta.uv_ptr;

    uint idx = indices[vertexID];

    float3 p = float3(
        positions[idx * 3 + 0],
        positions[idx * 3 + 1],
        positions[idx * 3 + 2]
    );

    float4 world = mul(inst.model, float4(p, 1.0));
    o.position = mul(push.view_proj, world);
    o.uv = float2(
        uvs[idx * 2 + 0],
        uvs[idx * 2 + 1]
    );
    o.material_id = meta.material_id;
    o.color = inst.color;
    return o;
}

[shader("fragment")]
float4 fs_main(VSOutput input) : SV_Target0
{
    MaterialData* material_table = (MaterialData*)push.material_table_ptr;
    MaterialData material = material_table[input.material_id];

    float4 base = material.base_color_factor;
    if(material.base_color_texture_id != 0xFFFFFFFF)
    {
        Texture2D tex = textures[material.base_color_texture_id];
        SamplerState samp = samplers[material.sampler_id];
        base *= tex.Sample(samp, input.uv);
    }

    return base * input.color;
}
