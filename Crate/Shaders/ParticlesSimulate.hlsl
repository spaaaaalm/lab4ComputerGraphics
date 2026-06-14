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

    p.Vel.y += 1.8f * gDeltaTime;

// 2. Турбулентность (покачивание)
float t = gTotalTime * 0.4f;
float3 turb = float3(
    sin(p.Pos.x * 0.8f + t) * 0.25f,
    0.0f,
    cos(p.Pos.z * 0.8f + t) * 0.25f
);
p.Vel += turb * gDeltaTime;

// 3. Сопротивление среды
p.Vel *= 0.975f;

// 4. Движение
p.Pos += p.Vel * gDeltaTime;

// 5. Визуал: рост + затухание
float lifeRatio = p.Age / p.Life;
p.Size = lerp(0.04f, 0.45f, lifeRatio);
p.Color.a = 1.0f - smoothstep(0.15f, 0.95f, lifeRatio);
p.Color.rgb = lerp(float3(0.95f, 0.95f, 1.0f), float3(0.55f, 0.55f, 0.65f), lifeRatio * 0.8f);

gParticlePool[idx] = p;
gAliveOut.Append(idx);
gSortList.Append(idx);
}