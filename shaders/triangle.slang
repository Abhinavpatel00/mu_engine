#ifndef DEBUG_PRINTF
#define DEBUG_PRINTF 0
#endif

struct PackedFace
{
  uint data0;
};

static const uint VOXEL_CHUNK_SIZE = 128;


struct Push
{
  uint64_t face_ptr;
  uint     face_count;
  float    aspect;
  float3   cam_pos;  // camera world position
  uint     pad1;     // alignment
  float3   cam_dir;  // camera forward (normalized)
  uint     pad2;
  float4x4 view_proj;
  uint     texture_id;
  uint     sampler_id;
};
static const float2 quadVerts[6] =
{
    float2(0,0),
    float2(1,0),
    float2(0,1),

    float2(0,1),
    float2(1,0),
    float2(1,1)
};
static const float3 normal_table[6] =
{
    float3(0, 1, 0),
    float3(0,-1, 0),
    float3(1, 0, 0),
    float3(-1,0, 0),
    float3(0, 0, 1),
    float3(0, 0,-1)
};
struct VSOutput
{
  float4 position : SV_Position;
  float2 uv  : TEXCOORD0;
  uint   mat : TEXCOORD1;

};

[[vk::binding(0, 0)]]
Texture2D textures[];

[[vk::binding(1, 0)]]
SamplerState samplers[];

[[vk::push_constant]]
Push push;


[shader("vertex")]
VSOutput vs_main(uint vertexID: SV_VertexID         ,

uint instanceID : SV_InstanceID

)
{
  VSOutput o;
uint *faces =(uint*)push.face_ptr;
uint face =faces[instanceID];
  float3 verts[3] = { float3(0.0, 0.0, 0.0), float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0) };

  float3 world = verts[vertexID];


    uint x = (face >> 27) & 31;
    uint y = (face >> 22) & 31;
    uint z = (face >> 17) & 31;

    uint n = (face >> 14) & 7;

    uint h = (face >> 10) & 15;
    uint w = (face >> 6)  & 15;

    uint material = face & 63;

    float2 q = quadVerts[vertexID];

    float3 pos = float3(x, y, z);

    if(n <= 1)              // top / bottom
        pos += float3(q.x * w, 0, q.y * h);

    else if(n <= 3)         // left / right
        pos += float3(0, q.y * h, q.x * w);

    else                    // front / back
        pos += float3(q.x * w, q.y * h, 0);

    o.position = mul(push.view_proj, float4(pos,1));
    o.uv = q;
    o.mat = material;
  return o;
}

[shader("fragment")]
float4 fs_main( 
VSOutput
input

)
    : SV_Target0
{
Texture2D tex=textures[push.texture_id];
SamplerState samp = samplers[push.sampler_id];
  return tex.Sample(samp,input.uv);
}
