struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vs_main(uint id : SV_VertexID)
{
    float2 p[3] =
    {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };

    VSOutput o;
    o.position = float4(p[id], 0.0, 1.0);
    o.uv = p[id] * 0.5 + 0.5;
    return o;
}

[shader("fragment")]
float4 fs_main(VSOutput input) : SV_Target0
{
    float2 uv = input.uv;
    float grid = step(0.98, frac(uv.x * 16.0)) + step(0.98, frac(uv.y * 16.0));
    float3 base = float3(uv.x, uv.y, 0.5 + 0.5 * sin((uv.x + uv.y) * 12.0));
    float3 color = lerp(base, float3(1.0, 1.0, 1.0), saturate(grid));
    return float4(color, 1.0);
}