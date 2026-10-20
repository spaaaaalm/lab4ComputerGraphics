Texture2D gCurrentColor : register(t0);   // Результат Lighting Pass
Texture2D gHistoryColor : register(t1);   // История с прошлого кадра
Texture2D gVelocity     : register(t2);   // GBuffer Target 4
SamplerState gsamPointClamp : register(s0);

cbuffer cbTaa : register(b0)
{
    float2 gInvRenderTargetSize;
    float gBlendWeight; // Обычно 0.1 (10% нового, 90% истории)
    float gPad;
}

struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VSOut VS(uint id : SV_VertexID)
{
    VSOut vout;
    float2 pos = float2((id << 1) & 2, id & 2);
    vout.TexC = pos;
    vout.PosH = float4(pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return vout;
}

// Простой 3x3 min/max clamp для борьбы с ghosting
float3 GetMin3x3(float2 uv)
{
    float3 minVal = 10000.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * gInvRenderTargetSize;
            minVal = min(minVal, gCurrentColor.SampleLevel(gsamPointClamp, uv + offset, 0).rgb);
        }
    }
    return minVal;
}

float3 GetMax3x3(float2 uv)
{
    float3 maxVal = -10000.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * gInvRenderTargetSize;
            maxVal = max(maxVal, gCurrentColor.SampleLevel(gsamPointClamp, uv + offset, 0).rgb);
        }
    }
    return maxVal;
}

float4 PS_TaaResolve(VSOut pin) : SV_Target
{
    // 1. Читаем velocity
    float2 vel = gVelocity.Sample(gsamPointClamp, pin.TexC).rg;
    
    // 2. Вычисляем UV в буфере истории
    float2 historyUV = pin.TexC + vel;
    
    // Ограничиваем, чтобы не выйти за границы текстуры
    historyUV = clamp(historyUV, 0.0, 1.0);
    
    // 3. Читаем цвета
    float3 current = gCurrentColor.Sample(gsamPointClamp, pin.TexC).rgb;
    float3 history = gHistoryColor.Sample(gsamPointClamp, historyUV).rgb;
    
    // 4. Neighborhood Clamping (AABB) 
    float3 minColor = GetMin3x3(pin.TexC);
    float3 maxColor = GetMax3x3(pin.TexC);
    history = clamp(history, minColor, maxColor);
    
    // 5. Блендинг
    float3 result = lerp(history, current, gBlendWeight);
    
    return float4(result, 1.0);
}