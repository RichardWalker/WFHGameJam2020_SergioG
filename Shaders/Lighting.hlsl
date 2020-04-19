#include "Common.hlsl"
#include "LightingCommon.hlsl"

ConstantBuffer<LightConstantsCB> cbLights : register(b2);

float3
pbrLighting(
   float roughness,
   float3 fromEye,
   float3 worldPos,
   float3 normal,
   float3 F0,
   float3 albedo,
   uint shadowBits)
{
   float3 radiance = float3(0, 0, 0);
   for (int lightIdx = 0; lightIdx < cbLights.numLights; ++lightIdx) {

      float far = cbLights.intensityBiasFar[lightIdx].z;
      float3 toLight = worldPos - cbLights.positions[lightIdx].xyz;
      float distanceToLight = length(toLight);

      float shadowFactor;
      if (shadowBits == -1) {  // No raytracing

         float bias = cbLights.intensityBiasFar[lightIdx].y;


         // PCF
         shadowFactor = 0.0f;
         #define NumSamples 8
         float ditherRadius = 0.05;

         for (int i = 0; i < NumSamples; ++i) {
            float3 random = iqhash(toLight.xy + i.xx) * ditherRadius;
            float sample = gCubes[cbLights.shadowMaps[lightIdx]].SampleLevel(pointSampler, toLight + random, 0).x;

            if (distanceToLight/far - sample < bias) {
               shadowFactor += 1.0f;
            }
         }
         shadowFactor /= NumSamples;
         #undef NumSamples

      }
      else {
         if (!(shadowBits & ( ( 1 << ( 31 - lightIdx))))) {
            shadowFactor = 0.0f;
         }
         else {
            shadowFactor = 1.0f;
         }
      }

      float3 lightDir = cbLights.positions[lightIdx].xyz - worldPos;
      lightDir /= length(lightDir);

      float3 halfVec = (fromEye + lightDir);
      halfVec /= length(halfVec);
      #define epsilon 1e-8
      float coshn = max(epsilon, dot(halfVec, normal));
      float cosnl = max(epsilon, dot(normal, lightDir));
      float coshl = max(epsilon, dot(halfVec, lightDir));
      float cosnv = max(epsilon, dot(normal, fromEye));

      float3 fresnel = F0 + (float3(1,1,1) - F0) * pow(1 - coshl, 5);

      float cosnv2 = cosnv*cosnv;
      float coshn2 = coshn*coshn;
      float a2 = roughness * roughness;

      float sqrtNdf = roughness / (coshn2 * (a2 - 1) + 1);
      float ndf = sqrtNdf * sqrtNdf;
      float G = 0.5f / (1 + sqrt( 1 + a2 *((1 / cosnv2) - 1)));
      float transmittedEnergy = length(1 - fresnel) / sqrt(3.0);
      float3 btdf = /*irradiance*/transmittedEnergy * /*exitance*/transmittedEnergy * albedo;
      float3 brdf = (ndf * fresnel * G) / (cosnl * cosnv);


      float falloff = clamp((far - distanceToLight) / far, 0, 1);
      falloff *= falloff;

      float intensity = cbLights.intensityBiasFar[lightIdx].x * falloff;
      radiance += (((btdf + brdf) / kPi) * intensity * cbLights.colors[lightIdx].rgb * cosnl) * shadowFactor;
   }

   return radiance;
}
