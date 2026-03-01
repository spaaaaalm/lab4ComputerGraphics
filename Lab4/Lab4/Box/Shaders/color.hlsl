Texture2D gDiffuseMap : register(t0);
SamplerState gSampler : register(s0);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldViewProj;
    float2   gTexOffset;    // дл€ текстурной анимации (UV scroll)
    float2   gTexScale;     // дл€ тайлинга
};

struct VertexIn
{
    float3 PosL : POSITION;
    float2 Tex  : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 Tex  : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);
    // ѕримен€ем тайлинг и анимацию
    vout.Tex = vin.Tex * gTexScale + gTexOffset;
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    // Sample автоматически выбирает mipmap уровень через ddx/ddy
    return gDiffuseMap.Sample(gSampler, pin.Tex);
}