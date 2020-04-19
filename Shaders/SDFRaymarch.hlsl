#define WriteDepthInPS
#include "Common.hlsl"
#include "Lighting.hlsl"

struct SDFReadCB
{
   int texBindIdx;
   float3 numVoxels;
   float3 worldDim;
};

ConstantBuffer<SDFReadCB> cbSDF : register(b0);
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

float decodeDistance(float encodedDist)
{
   float voxelSize = (cbSDF.worldDim / cbSDF.numVoxels);
   float range = 4*voxelSize;

   // float distance = (encodedDist * 2*range - range) * voxelSize;
   float dist = encodedDist - 0.5;  // [ -0.5, 0.5 ]
   dist *= range;  // [ -range / 2, range / 2 ]
   return dist;
}

float
sdf(float3 position)
{
   float distance = 1.0f;
   if (abs(position.x) < cbSDF.worldDim.x / 2.0f &&
       abs(position.y) < cbSDF.worldDim.y / 2.0f &&
       abs(position.z) < cbSDF.worldDim.z / 2.0f) {
   #if 0
      int3 sdfIdx = int3(cbSDF.numVoxels * ((position / (2*cbSDF.worldDim)) + float3(0.5,0.5,0.5)));
      distance = gVolumes[cbSDF.texBindIdx].Load(float4(sdfIdx, 0));
   #else
      float3 texcoord = (position / (cbSDF.worldDim)) + float3(0.5,0.5,0.5);
      distance = decodeDistance(gVolumes[cbSDF.texBindIdx].SampleLevel(linearSampler, texcoord, 0).r);
   #endif
   }
   return distance;
}

float3
sdfNormal(float3 p)
{
   float3 texcoord = (p / (cbSDF.worldDim)) + float3(0.5,0.5,0.5);
   float2 d = float2(1.0 / cbSDF.numVoxels.x, 0);

   float x = decodeDistance(gVolumes[cbSDF.texBindIdx].SampleLevel(linearSampler, texcoord + d.xyy, 0).r);
   x -= decodeDistance(gVolumes[cbSDF.texBindIdx].SampleLevel(linearSampler, texcoord - d.xyy, 0).r);
   float y = decodeDistance(gVolumes[cbSDF.texBindIdx].SampleLevel(linearSampler, texcoord + d.yxy, 0).r);
   y -= decodeDistance(gVolumes[cbSDF.texBindIdx].SampleLevel(linearSampler, texcoord - d.yxy, 0).r);
   float z = decodeDistance(gVolumes[cbSDF.texBindIdx].SampleLevel(linearSampler, texcoord + d.yyx, 0).r);
   z -= decodeDistance(gVolumes[cbSDF.texBindIdx].SampleLevel(linearSampler, texcoord - d.yyx, 0).r);

   float3 n = normalize(float3(x, y, z));
   return n;
}

PSOutput
pixelMain(PSInput input)
{
   PSOutput res = (PSOutput)0;
   float4 screenPos = float4(input.worldPos.xy, 0, 1);
   float4 pos = mul(cbMaterial.toWorld, screenPos);
   pos /= pos.w;
   // Ray
   float3 o = cbMaterial.camPos;
   float3 d = normalize(pos.xyz - o);

   float minT = 0.0f;
   for (int i = 0; i < kMaxStep; ++i) {
      float3 worldPos = o + minT * d;
      float dist = sdf(worldPos);
      if (dist < 0.0) {
         float outT = minT;
         float outDist = dist;
         do {
            outT -= outDist;
            outDist = sdf(o + outT * d);
         } while (outDist <= 0.0);

         float3 normal = sdfNormal(worldPos);
         float3 fromEye = cbMaterial.camPos - worldPos.xyz;

         uint shadowBits = 0xffffffff;  // TODO: Ray-trace into blob

         #if 0  // Commenting out while I straighten out the bindless SRV mapping story...
         float3 radiance = pbrLighting(
                              cbMaterial.roughness,
                              fromEye,
                              worldPos,
                              normal,
                              cbMaterial.specularColor.rgb,
                              cbMaterial.albedo.rgb,
                              shadowBits);
         #else
         float3 radiance = float3(0,0,0);
         #endif

         res.color.rgb = toGamma(radiance);

         float z = (dot(minT*d, cbMaterial.camDir));
         res.depth = cbMaterial.far * (z - cbMaterial.near) / (z * (cbMaterial.far - cbMaterial.near));

         return res;
      }
      minT += max(dist, 1/cbSDF.numVoxels.x);
   }

   discard;
   return res;
}