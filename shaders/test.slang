// // simple.slang
//
// struct VSInput
// {
//     float3 position : POSITION;
//     float3 color    : COLOR;
// };
//
// struct VSOutput
// {
//     float4 position : SV_Position;
//     float3 color    : COLOR;
// };
//
// // =======================
// // Vertex Shader
// // =======================
//
// [shader("vertex")]
// VSOutput vs_main(VSInput input)
// {
//     VSOutput output;
//
//     output.position = float4(input.position, 1.0);
//     output.color    = input.color;
//
//     return output;
// }
//
// // =======================
// // Fragment Shader
// // =======================
//
// [shader("fragment")]
// float4 fs_main(VSOutput input) : SV_Target0
// {
//     return float4(input.color, 1.0);
// }







struct VSOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VSOutput vs_main(uint id : SV_VertexID)
{
    float2 pos[3] =
    {
        float2(-1, -1),
        float2( 3, -1),
        float2(-1,  3)
    };

    VSOutput o;
    o.position = float4(pos[id], 0, 1);
    return o;
}

[shader("fragment")]
float4 fs_main() : SV_Target0
{
    return float4(0.0, 0.7, 1.0, 1.0);
}






