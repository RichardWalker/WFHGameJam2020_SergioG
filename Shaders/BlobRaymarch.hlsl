#define WriteDepthInPS
#include "Common.hlsl"
#include "Lighting.hlsl"
#include "BlobCommon.hlsl"

ConstantBuffer<BlobCB> cbBlob : register(b0);
ConstantBuffer<MaterialConstantsCB> cbMaterial : register(b1);

PSInput
vertexMain(float4 position : POSITION, float2 texcoord : TEXCOORD, float3 normal : NORMAL)
{
   PSInput result;

   result.worldPos = mul(cbMaterial.objectTransform, position);
   result.screenPos = mul(cbMaterial.viewProjection, result.worldPos);
   result.uv = texcoord;
   result.normal = mul(cbMaterial.objectTransform, float4(normal, 1)).rgb;
   result.normal /= length(normal);

   return result;
}

PSOutput
pixelMain(PSInput input)
{
   PSOutput res = (PSOutput)0;
   float4 pos = float4(input.worldPos.xy, 0, 1);
   pos = mul(cbMaterial.toWorld, pos);
   pos /= pos.w;
   // Ray
   float3 o = cbMaterial.camPos;
   float3 d = normalize(pos.xyz - o);

   float minT = 0.0f;

   for (int i = 0; i < kMaxStep; ++i) {
      float3 worldPos = o + minT * d;
      float dist = distanceToBlob(cbBlob, worldPos);
      if (dist < kEpsilon) {
         float3 normal = blobNormal(cbBlob, worldPos);
         float3 fromEye = cbMaterial.camPos - worldPos.xyz;
         res.color.rgb = toGamma(pbrLighting(cbMaterial.roughness, fromEye, worldPos, normal, cbMaterial.specularColor, cbMaterial.albedo));
         res.color.a = 1.0f;
         float z = (dot(minT*d, cbMaterial.camDir));
         res.depth = cbMaterial.far * (z - cbMaterial.near) / (z * (cbMaterial.far - cbMaterial.near));
         return res;
      }
      minT += dist;
   }
   discard;
   return res;
}