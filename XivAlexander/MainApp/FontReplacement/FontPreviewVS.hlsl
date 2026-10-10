// The vertex shader of the settings' font previews (PreviewRenderer): passes each vertex through as FontEdgePS.hlsl takes
// it, so that the preview draws edges with the shader the game does. The game's FontEdgeVS computes the same values from
// its own vertex data; here they are computed on the CPU, with the position already in clip space.

struct VSInput
{
    float2 Position : POSITION;
    float4 Color : COLOR0;
    float4 Channel : COLOR1;
    float2 Uv : TEXCOORD0;
    float2 RectMin : TEXCOORD1;
    float2 RectMax : TEXCOORD2;
    float4 Step : TEXCOORD3;
};

// FontEdgePS.hlsl's PSInput, in the same order.
struct PSInput
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float4 Channel : COLOR1;
    float2 Uv : TEXCOORD0;
    float2 RectMin : TEXCOORD1;
    float2 RectMax : TEXCOORD2;
    float4 Step : TEXCOORD3;
};

PSInput main(VSInput input)
{
    PSInput output;
    output.Position = float4(input.Position, 0, 1);
    output.Color = input.Color;
    output.Channel = input.Channel;
    output.Uv = input.Uv;
    output.RectMin = input.RectMin;
    output.RectMax = input.RectMax;
    output.Step = input.Step;
    return output;
}
