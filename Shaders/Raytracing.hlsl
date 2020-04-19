#include "Common.hlsl"
#include "LightingCommon.hlsl"

struct Attributes
{
   float2 uv;
};

struct HitInfo
{
   int shadow;
};

struct RayGenCB
{
   float4x4 toWorld;
   float4 origin;
   float2 resolution;
   int outputBindIdx;
};

ConstantBuffer<RayGenCB> cb : register(b0);
ConstantBuffer<LightConstantsCB> cbLights : register(b1);

RaytracingAccelerationStructure SceneBVH  : register(t0);

#define ShadowTexture gUAVInt[cb.outputBindIdx]

[shader("raygeneration")]
void RayGen()
{
   uint2 LaunchIndex = DispatchRaysIndex().xy;
   uint2 LaunchDimensions = DispatchRaysDimensions().xy;

   float2 d = (((LaunchIndex.xy + 0.5f) / cb.resolution.xy) * float2(2, -2) + float2(-1.f, 1.f));

   // Setup the ray
   RayDesc ray;
   ray.Origin = cb.origin.xyz;

   float4 rayStart = mul(cb.toWorld, float4(d.xy, 0, 1));
   rayStart /= rayStart.w;

   float4 rayEnd = mul(cb.toWorld, float4(d.xy, 1, 1));
   rayEnd /= rayEnd.w;


   float3 rayDir = rayEnd.xyz - rayStart.xyz;
   float rayLen = length(rayDir);
   ray.Direction = rayDir / rayLen;

   ray.TMin = 0;
   ray.TMax = rayLen;

   // Trace the ray
   HitInfo payload;
   TraceRay(
      SceneBVH,
      RAY_FLAG_NONE,
      0xFF,
      /*hit group idx*/0,
      /*geometry multiplier for idx*/0,
      /*miss shader*/0,
      ray,
      payload);
}

[shader("miss")]
void ShadowMiss(inout HitInfo payload)
{
   payload.shadow = 1;
}

[shader("closesthit")]
void ShadowHit(inout HitInfo payload, Attributes attrib)
{
   payload.shadow = 0;
}

[shader("closesthit")]
void TraceShadowRays(inout HitInfo payload, Attributes attrib)
{
   uint triangleIndex = PrimitiveIndex();
   float3 barycentrics = float3((1.0f - attrib.uv.x - attrib.uv.y), attrib.uv.x, attrib.uv.y);

   float3 hitLocation = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

   ShadowTexture[DispatchRaysIndex().xy] = 0;

   for (int lightIdx = 0; lightIdx < cbLights.numLights; ++lightIdx) {
      RayDesc ray;
      ray.Origin = hitLocation;
      float3 pos = cbLights.positions[lightIdx].xyz;
      ray.Direction = pos - hitLocation;
      float dirLen = length(ray.Direction);
      ray.Direction /= dirLen;

      ray.TMin = 1e-3;
      ray.TMax = dirLen;

      HitInfo shadowPayload;
      TraceRay(
         SceneBVH,
         RAY_FLAG_NONE,
         0xFF,
         /*hit group idx*/1,
         /*geometry multiplier for idx*/0,
         /*miss shader*/1,
         ray,
         shadowPayload);
      ShadowTexture[DispatchRaysIndex().xy] += ( 1 << (31 - lightIdx) ) * shadowPayload.shadow;
   }
}

[shader("miss")]
void Miss(inout HitInfo payload)
{
   ShadowTexture[DispatchRaysIndex().xy] = 0;
}