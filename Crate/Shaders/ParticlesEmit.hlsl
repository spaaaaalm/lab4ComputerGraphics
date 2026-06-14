struct Particle
{
    float3 Pos;
    float Age;
    float3 Vel;
    float Life;
    float4 Color;
    float Size;
    float3 Pad;
};

cbuffer ParticleCB : register(b0)
{
    float gDeltaTime;
    float gTotalTime;
    float gEmitRate;
    float gGravity;
    float3 gEmitterPos;
    float gMaxLife;
    uint gConsumeCount;
    uint gEmitCount;
    uint gMaxParticles;
    uint gPad;
};

RWStructuredBuffer<Particle> gParticlePool : register(u0);
AppendStructuredBuffer<uint> gAliveOut : register(u2);
ConsumeStructuredBuffer<uint> gDeadList : register(u3);
AppendStructuredBuffer<uint> gSortList : register(u4);

// Хеш-функция для псевдослучайных чисел в шейдере
float Hash01(uint x)
{
    x ^= x >> 17;
    x *= 0xed5ad4bbU;
    x ^= x >> 11;
    x *= 0xac4c1b51U;
    x ^= x >> 15;
    x *= 0x31848babU;
    x ^= x >> 14;
    return (x & 0x00ffffff) / 16777215.0f;
}

[numthreads(256, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    uint i = dtid.x;
    if (i >= gEmitCount)
        return;

    uint seed = i + (uint)(gTotalTime * 1000.0f) * 747796405u;

    // Параметры разброса при рождении
    float angle = Hash01(seed) * 6.2831853f;
    float radius = Hash01(seed * 3u + 11u) * 0.25f;
    float upSpeed = lerp(0.8f, 1.4f, Hash01(seed * 7u + 23u));

    // Позиция: эмиттер + небольшой круговой разброс
    float3 offset = float3(cos(angle) * radius, 0.0f, sin(angle) * radius);

    Particle p;
    p.Pos = gEmitterPos + offset;

    // Скорость: вверх + лёгкий горизонтальный шум
    p.Vel = float3(
        (Hash01(seed * 13u) - 0.5f) * 0.3f,
        upSpeed,
        (Hash01(seed * 17u) - 0.5f) * 0.3f
    );

    // Время жизни (минимум 1.5 сек, чтобы не исчезали мгновенно)
    p.Age = 0.0f;
    p.Life = max(gMaxLife * lerp(0.8f, 1.2f, Hash01(seed * 23u)), 1.5f);
    
    // Начальный цвет и размер
    p.Color = float4(0.92f, 0.92f, 0.98f, 1.0f);
    p.Size = 0.04f;
    p.Pad = 0.0f;

    // Берём индекс из мёртвого списка и инициализируем
    uint particleIndex = gDeadList.Consume();
    gParticlePool[particleIndex] = p;
    gAliveOut.Append(particleIndex);
    gSortList.Append(particleIndex);
}