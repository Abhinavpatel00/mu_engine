struct LightBeam
{
  float4 pos_width;
  float4 sun_height;
  float4 misc;
};

struct Push
{
  uint64_t beam_ptr;
  float4x4 view_proj;
};

[[vk::push_constant]]
Push push;


// device-address buffer
// vertex → fragment data
struct VSOutput
{
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
  float  opacity : TEXCOORD1;
};


// quad generated in shader
static const float2 quad[6] = { float2(-1, -1), float2(1, -1), float2(1, 1),
                                float2(-1, -1), float2(1, 1),  float2(-1, 1) };


// --------------------
// Vertex Shader
// --------------------
[shader("vertex")]
VSOutput vs_main(uint vertexID: SV_VertexID, uint instanceID: SV_InstanceID)
{
  VSOutput o;

  let       beams = (LightBeam*)push.beam_ptr;
  LightBeam beam  = beams[instanceID];

  float2 q = quad[vertexID];

  float3 beam_pos = beam.pos_width.xyz;
  float  beam_w   = beam.pos_width.w;

  float3 sun      = normalize(beam.sun_height.xyz);
  float  beam_h   = beam.sun_height.w;

  float  opacity  = beam.misc.x;

  // avoid cross-product degeneracy
  float3 upAxis = abs(sun.y) > 0.9 ? float3(1, 0, 0) : float3(0, 1, 0);

  float3 right = normalize(cross(sun, upAxis));
  float3 up    = normalize(cross(right, sun));

  float3 worldPos = beam_pos + right * q.x * beam_w + up * q.y * beam_h;

  o.pos = mul(push.view_proj, float4(worldPos, 1));

  o.uv      = q * 0.5 + 0.5;
  o.opacity = opacity;

  return o;
}


// --------------------
// Fragment Shader
// --------------------
[shader("fragment")]
float4 fs_main(VSOutput input)
    : SV_Target0
{
  float3 beamColor = float3(1.0, 0.9, 0.65);

  float gradient = 1.0 - input.uv.y;
  float center   = 1.0 - abs(input.uv.x * 2.0 - 1.0);

  float alpha = gradient * center * input.opacity;

  return float4(beamColor, alpha);
}
