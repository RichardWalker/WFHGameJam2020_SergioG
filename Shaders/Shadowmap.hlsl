#include "Common.hlsl"

struct ShadowCB
{
   float4x4 viewProjection;
   float4x4 objectTransform;
   float far;
   float3 lightPos;
};


ConstantBuffer<ShadowCB> cbShadow : register(b0);

PSInput
vertexMain(float4 position : POSITION, float2 texcoord : TEXCOORD, float3 normal : NORMAL)
{
   PSInput result;

   result.worldPos = mul(cbShadow.objectTransform, position);
   result.screenPos = mul(cbShadow.viewProjection, result.worldPos);

   return result;
}

PSOutput
pixelMain(PSInput input)
{
   PSOutput output;

   float3 diff = input.worldPos - cbShadow.lightPos;

   output.color.r = length(diff) / cbShadow.far;

   return output;
}

