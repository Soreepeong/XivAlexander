// The text pass of the settings' font previews (PreviewRenderer), drawn over the edge pass: the glyph's coverage in its
// channel of the atlas, in the text color. Each pixel is at a texel's center, so the linear sampler reads the texel as is.

Texture2D g_TextureT : register(t0);
SamplerState g_TextureS : register(s0);

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

float4 main(PSInput input) : SV_TARGET
{
    float coverage = dot(g_TextureT.SampleLevel(g_TextureS, input.Uv, 0), input.Channel);
    return float4(input.Color.rgb, input.Color.a * coverage);
}
