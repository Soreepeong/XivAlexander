// Font preview VS (PreviewRenderer): passes through FontEdgePS.hlsl inputs, computed on the CPU (clip space) as the game's FontEdgeVS does.

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
