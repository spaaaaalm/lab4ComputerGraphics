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
    float gFloorY;

    float gRestitution;
    float gFloorFriction;
    float gBounceStopVelocity;
    float gPad;
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

    uint seed = i + (uint)(gTotalTime * 1000.0f) * 747796405u;

    float angle = Hash01(seed) * 6.2831853f;
    float radius = Hash01(seed * 3u + 11u) * 0.35f;

    // Spawn as a fountain: particles go up first, then gravity pulls them down.
    float upSpeed = lerp(4.0f, 7.5f, Hash01(seed * 7u + 23u));
    float sideSpeed = lerp(1.0f, 3.5f, Hash01(seed * 19u + 41u));

    float3 dirXZ = float3(cos(angle), 0.0f, sin(angle));
    float3 offset = dirXZ * radius;

    Particle p;
    p.Pos = gEmitterPos + offset;
    p.Vel = dirXZ * sideSpeed;
    p.Vel.y = upSpeed;

    p.Age = 0.0f;
    p.Life = max(gMaxLife * lerp(0.75f, 1.25f, Hash01(seed * 23u)), 1.5f);

    p.Color = float4(0.92f, 0.92f, 0.98f, 1.0f);
    p.Size = 0.04f;
    p.Pad = 0.0f;

    uint particleIndex = gDeadList.Consume();
    gParticlePool[particleIndex] = p;
    gAliveOut.Append(particleIndex);
    gSortList.Append(particleIndex);
}
