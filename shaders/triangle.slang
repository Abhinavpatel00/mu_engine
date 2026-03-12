#ifndef DEBUG_PRINTF
#define DEBUG_PRINTF 0
#endif

struct Push
{
    uint64_t face_ptr;
    uint64_t mat_ptr;

    uint     face_count;
    float    aspect;

    float3   cam_pos;
    uint     pad1;

    float3   cam_dir;
    uint     pad2;

    float4x4 view_proj;

    uint texture_id;
    uint sampler_id;
};

struct Material
{
    uint side;
    uint top;
    uint bottom;
};

static const float2 quadVerts[4] =
{
    float2(0,0),
    float2(1,0),
    float2(0,1),
    float2(1,1)
};

static const uint FACE_POS_X = 0;
static const uint FACE_NEG_X = 1;
static const uint FACE_POS_Y = 2;
static const uint FACE_NEG_Y = 3;
static const uint FACE_POS_Z = 4;
static const uint FACE_NEG_Z = 5;

static const float3 normal_table[6] =
{
    float3(1, 0, 0),
    float3(-1,0, 0),
    float3(0, 1, 0),
    float3(0,-1, 0),
    float3(0, 0, 1),
    float3(0, 0,-1)
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    Material mat    : TEXCOORD1;
    uint normal     : TEXCOORD2;
};

[[vk::binding(0, 0)]]
Texture2D textures[];

[[vk::binding(1, 0)]]
SamplerState samplers[];

[[vk::push_constant]]
Push push;


[shader("vertex")]
VSOutput vs_main(uint vertexID : SV_VertexID,
                 uint instanceID : SV_InstanceID)
{
    VSOutput o;

    uint* faces = (uint*)push.face_ptr;
    Material* materials = (Material*)push.mat_ptr;

    uint face = faces[instanceID];

    uint x = (face >> 27) & 31;
    uint y = (face >> 22) & 31;
    uint z = (face >> 17) & 31;

    uint n = (face >> 14) & 7;

    uint h = (face >> 10) & 15;
    uint w = (face >> 6)  & 15;

    uint material_id = face & 63;

    Material mat = materials[material_id];

    float2 quad = quadVerts[vertexID];

    float3 voxel_pos = float3(x, y, z);

    float3 normal = normal_table[n];

    float3 tangent;
    float3 bitangent;

    if(abs(normal.y) == 1)
    {
        tangent   = float3(1,0,0);
        bitangent = float3(0,0,1);
    }
    else if(abs(normal.x) == 1)
    {
        tangent   = float3(0,1,0);
        bitangent = float3(0,0,1);
    }
    else
    {
        tangent   = float3(1,0,0);
        bitangent = float3(0,1,0);
    }

    float3 pos = voxel_pos;

    if(normal.x > 0) pos.x += 1;
    if(normal.y > 0) pos.y += 1;
    if(normal.z > 0) pos.z += 1;

    pos += tangent * quad.x * w;
    pos += bitangent * quad.y * h;

    o.position = mul(push.view_proj, float4(pos,1));

    o.uv = quad;
    o.mat = mat;
    o.normal = n;

    return o;
}


[shader("fragment")]
float4 fs_main(VSOutput input) : SV_Target0
{
    uint texID;

    Material mat = input.mat;

    if(input.normal == FACE_POS_Y)
        texID = mat.top;
    else if(input.normal == FACE_NEG_Y)
        texID = mat.bottom;
    else
        texID = mat.side;

    Texture2D tex = textures[texID];
    SamplerState samp = samplers[push.sampler_id];

    float3 color = tex.Sample(samp, input.uv).rgb;

    return float4(color,1);
}
