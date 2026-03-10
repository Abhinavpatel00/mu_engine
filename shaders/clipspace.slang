struct VSOutput
{
    float4 position : SV_Position;
    float2 clipXY   : TEXCOORD0;
};

[shader("vertex")]
VSOutput vs_main(uint id : SV_VertexID)
{
    VSOutput o;

    float2 verts[3] =
    {
        float2(-1,-1),
        float2( 3,-1),
        float2(-1, 3)
    };

    float2 p = verts[id];

    o.position = float4(p,0,1);
    o.clipXY   = p;

    return o;
}




float4 fs_main(float4 pos : SV_Position) : SV_Target0
{
    float2 uv = pos.xy / float2(1920,1080);

    return float4(uv,0,1);
}
