Texture2D gTex0 : register(t0);
Texture2D gTex1 : register(t1);
SamplerState gSampler : register(s0);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldViewProj;
    float2   gTexOffset;
    float2   gTexScale;
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
    // Пол НЕ двигается — не применяем gTexOffset
    vout.Tex = vin.Tex;
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    float tileCount = 8.0;
    float2 scaled = pin.Tex * tileCount;
    int check = ((int)floor(scaled.x) + (int)floor(scaled.y)) % 2;

    float4 c0 = gTex0.Sample(gSampler, pin.Tex * tileCount);
    float4 c1 = gTex1.Sample(gSampler, pin.Tex * tileCount);

    return (check == 0) ? c0 : c1;
}