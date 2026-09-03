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

    uint seed =
        i +
        (uint)(gTotalTime * 1000.0f) *
        747796405u;

    float randomA = Hash01(seed);
    float randomB = Hash01(seed * 3u + 11u);
    float randomC = Hash01(seed * 7u + 23u);
    float randomD = Hash01(seed * 13u + 31u);
    float randomE = Hash01(seed * 19u + 41u);

    float angle = randomA * 6.2831853f;

    float radius =
        lerp(0.15f, 0.75f, randomB);

    float radialSpeed =
        lerp(0.2f, 0.8f, randomC);

    float verticalSpeed =
        lerp(1.0f, 2.8f, randomD);

    float rotationSpeed =
        lerp(1.5f, 3.5f, randomE);

    float3 radialDirection =
        float3(
            cos(angle),
            0.0f,
            sin(angle));

    float3 tangentDirection =
        float3(
            -sin(angle),
            0.0f,
            cos(angle));

    Particle p;

    p.Pos =
        gEmitterPos +
        radialDirection * radius;

    p.Age = 0.0f;

    // Частица получает движение наружу,
    // движение вверх и движение по касательной.
    p.Vel =
        radialDirection * radialSpeed +
        tangentDirection * rotationSpeed +
        float3(0.0f, verticalSpeed, 0.0f);

    p.Life =
        lerp(1.8f, gMaxLife, randomA);

    // Голубая, синяя или фиолетовая палитра.
    float colorMix = randomB;

    float3 cyanColor =
        float3(0.05f, 0.85f, 1.0f);

    float3 violetColor =
        float3(0.55f, 0.15f, 1.0f);

    float3 particleColor =
        lerp(cyanColor, violetColor, colorMix);

    p.Color =
        float4(particleColor, 1.0f);

    // Размер немного меньше, чем у старых частиц.
    p.Size =
        lerp(0.10f, 0.28f, randomC);

    p.Pad =
        0.0f.xxx;

    uint particleIndex =
        gDeadList.Consume();

    gParticlePool[particleIndex] =
        p;

    gAliveOut.Append(particleIndex);
    gSortList.Append(particleIndex);
}