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
ConsumeStructuredBuffer<uint> gAliveIn : register(u1);
AppendStructuredBuffer<uint> gAliveOut : register(u2);
AppendStructuredBuffer<uint> gDeadList : register(u3);
AppendStructuredBuffer<uint> gSortList : register(u4);

[numthreads(256, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    uint i = dtid.x;
    if (i >= gConsumeCount)
        return;

    uint idx = gAliveIn.Consume();
    Particle p = gParticlePool[idx];

    p.Age += gDeltaTime;
    if (p.Age >= p.Life)
    {
        gDeadList.Append(idx);
        return;
    }

    float lifeRatio = saturate(p.Age / p.Life);

    // Visuals.
    p.Size = lerp(0.04f, 0.45f, lifeRatio);
    p.Color.a = 1.0f - smoothstep(0.15f, 0.95f, lifeRatio);
    p.Color.rgb = lerp(
        float3(0.95f, 0.95f, 1.0f),
        float3(0.55f, 0.55f, 0.65f),
        lifeRatio * 0.8f);

    // Physics.
    p.Vel.y += gGravity * gDeltaTime;

    float t = gTotalTime * 0.4f;
    float3 turb = float3(
        sin(p.Pos.x * 0.8f + t) * 0.25f,
        0.0f,
        cos(p.Pos.z * 0.8f + t) * 0.25f);
    p.Vel += turb * gDeltaTime;

    // Frame-rate independent damping. 0.975 is old per-60-FPS-frame damping.
    p.Vel *= pow(0.975f, gDeltaTime * 60.0f);

    p.Pos += p.Vel * gDeltaTime;

    // Floor collision. Size is used as approximate particle radius.
    float radius = p.Size;
    if (p.Pos.y - radius < gFloorY)
    {
        p.Pos.y = gFloorY + radius;

        if (p.Vel.y < 0.0f)
        {
            p.Vel.y = -p.Vel.y * gRestitution;
            p.Vel.xz *= gFloorFriction;

            if (abs(p.Vel.y) < gBounceStopVelocity)
                p.Vel.y = 0.0f;
        }
    }

    gParticlePool[idx] = p;
    gAliveOut.Append(idx);
    gSortList.Append(idx);
}
