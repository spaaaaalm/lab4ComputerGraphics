Texture2D gPositionMap : register(t0);
Texture2D gNormalMap   : register(t1);
Texture2D gAlbedoMap   : register(t2);

SamplerState gSampler : register(s0);

#define MAX_DIRECTIONAL_LIGHTS 4
#define MAX_POINT_LIGHTS 32
#define MAX_SPOT_LIGHTS 32

struct DirectionalLight
{
    float3 Direction;
    float Padding1;
    float3 Color;
    float Intensity;
};

struct PointLight
{
    float3 Position;
    float Radius;
    float3 Color;
    float Intensity;
};

struct SpotLight
{
    float3 Position;
    float Radius;
    float3 Direction;
    float SpotAngle;
    float3 Color;
    float Intensity;
    float InnerAngle;
    float3 Padding;
};

cbuffer cbLighting : register(b0)
{
    DirectionalLight gDirLights[MAX_DIRECTIONAL_LIGHTS];
    PointLight gPointLights[MAX_POINT_LIGHTS];
    SpotLight gSpotLights[MAX_SPOT_LIGHTS];
    
    float3 gCameraPosition;
    int gNumDirectionalLights;
    
    int gNumPointLights;
    int gNumSpotLights;
    float2 gScreenDimensions;
    
    float3 gAmbientLight;
    float Padding;
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
    vout.PosH = float4(vin.PosL, 1.0f);
    vout.Tex = vin.Tex;
    return vout;
}

float3 ComputeDirectionalLight(DirectionalLight light, float3 normal, float3 viewDir)
{
    float3 lightDir = normalize(-light.Direction);
    float NdotL = max(dot(normal, lightDir), 0.0f);
    float3 diffuse = NdotL * light.Color * light.Intensity;
    
    float3 halfVec = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVec), 0.0f);
    float3 specular = pow(NdotH, 32.0f) * light.Color * light.Intensity * 0.5f;
    
    return diffuse + specular;
}

float3 ComputePointLight(PointLight light, float3 position, float3 normal, float3 viewDir)
{
    float3 lightVec = light.Position - position;
    float distance = length(lightVec);
    
    if (distance > light.Radius)
        return float3(0.0f, 0.0f, 0.0f);
    
    float3 lightDir = lightVec / distance;
    float attenuation = saturate(1.0f - (distance / light.Radius));
    attenuation *= attenuation;
    
    float NdotL = max(dot(normal, lightDir), 0.0f);
    float3 diffuse = NdotL * light.Color * light.Intensity * attenuation;
    
    float3 halfVec = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVec), 0.0f);
    float3 specular = pow(NdotH, 32.0f) * light.Color * light.Intensity * attenuation * 0.5f;
    
    return diffuse + specular;
}

float3 ComputeSpotLight(SpotLight light, float3 position, float3 normal, float3 viewDir)
{
    float3 lightVec = light.Position - position;
    float distance = length(lightVec);
    
    if (distance > light.Radius)
        return float3(0.0f, 0.0f, 0.0f);
    
    float3 lightDir = lightVec / distance;
    float cosAngle = dot(-lightDir, normalize(light.Direction));
    
    if (cosAngle < light.SpotAngle)
        return float3(0.0f, 0.0f, 0.0f);
    
    float spotAttenuation = saturate((cosAngle - light.SpotAngle) / (light.InnerAngle - light.SpotAngle));
    float distAttenuation = saturate(1.0f - (distance / light.Radius));
    distAttenuation *= distAttenuation;
    
    float totalAttenuation = spotAttenuation * distAttenuation;
    
    float NdotL = max(dot(normal, lightDir), 0.0f);
    float3 diffuse = NdotL * light.Color * light.Intensity * totalAttenuation;
    
    float3 halfVec = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVec), 0.0f);
    float3 specular = pow(NdotH, 32.0f) * light.Color * light.Intensity * totalAttenuation * 0.5f;
    
    return diffuse + specular;
}

float4 PS(VertexOut pin) : SV_Target
{
    float3 position = gPositionMap.Sample(gSampler, pin.Tex).xyz;
    float3 normal = normalize(gNormalMap.Sample(gSampler, pin.Tex).xyz);
    float3 albedo = gAlbedoMap.Sample(gSampler, pin.Tex).rgb;
    
    if (length(normal) < 0.1f)
        return float4(0.1f, 0.1f, 0.15f, 1.0f);
    
    float3 viewDir = normalize(gCameraPosition - position);
    float3 lighting = gAmbientLight;
    
    for (int i = 0; i < gNumDirectionalLights; ++i)
        lighting += ComputeDirectionalLight(gDirLights[i], normal, viewDir);
    
    for (int j = 0; j < gNumPointLights; ++j)
        lighting += ComputePointLight(gPointLights[j], position, normal, viewDir);
    
    for (int k = 0; k < gNumSpotLights; ++k)
        lighting += ComputeSpotLight(gSpotLights[k], position, normal, viewDir);
    
    float3 finalColor = albedo * lighting;
    finalColor = finalColor / (finalColor + 1.0f);
    finalColor = pow(finalColor, 1.0f / 2.2f);
    
    return float4(finalColor, 1.0f);
}