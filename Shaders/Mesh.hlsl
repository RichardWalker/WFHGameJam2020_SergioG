#include "Common.hlsl"
#include "Lighting.hlsl"

// ConstantBuffer<MaterialConstantsCB> cbShader : register(b1);

PSInput
vertexMain(float4 position : POSITION, float2 texcoord : TEXCOORD, float3 normal : NORMAL)
{
   PSInput result;

   result.worldPos = mul(cbShader.objectTransform, position);
   result.screenPos = mul(cbShader.viewProjection, result.worldPos);
   result.uv = texcoord;
   result.normal = mul(cbShader.objectTransform, float4(normal, 0)).rgb;

   return result;
}

PSOutput
pixelMain(PSInput input)
{
   PSOutput psout;

   float3 albedo;

   if (cbShader.useFlatColor) {
      albedo = cbShader.albedo.rgb;
   }
   else {
      albedo = gTextures[cbShader.diffuseTexIdx].Sample(linearSampler, input.uv).rgb;
   }

   albedo = toLinear(albedo);

   float3 F0 = cbShader.specularColor.rgb;

   // Compute lighting
   float3 fromEye = cbShader.camPos - input.worldPos.xyz;
   fromEye /= length(fromEye);

   uint shadowBits = gUAVInt[cbShader.raytracedShadowIdx][int2(input.screenPos.xy - 0.5)];
   if (!cbShader.useRaytracedShadows) {
      shadowBits = 0xffffffff;
   }

   input.normal /= length(input.normal) ;

   float3 radiance = pbrLighting(
                        cbShader.roughness,
                        fromEye,
                        input.worldPos.xyz,
                        input.normal,
                        F0,
                        albedo,
                        shadowBits);

   radiance += iqhash(input.screenPos.xy) * (0.6 / 255);  // 0.2 / 255 is a good value to prevent it from being visible in a dark screen.
   // radiance += valveDither(input.screenPos);

   psout.color = float4(saturate(toGamma(radiance)), 1.0f);

   return psout;
}