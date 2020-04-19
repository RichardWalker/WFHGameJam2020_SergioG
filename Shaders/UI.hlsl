#include "Common.hlsl"

struct TextConstants
{
   matrix<float, 4, 4> transform;

   int atlasBindIdx;
};

ConstantBuffer<TextConstants> cbText : register(b0);

PSInputScreen vertexMain(float4 position : POSITION, float2 texcoord : TEXCOORD, float3 normal : NORMAL, float4 color : COLOR)
{
   PSInputScreen result;

   result.screenPos = mul(cbText.transform, position);  // TODO: get object transform out of viewproj
   result.uv = texcoord;
   result.color = color;
   return result;
}

PSOutput pixelMain(PSInputScreen input)
{
   PSOutput psout;

   if (cbText.atlasBindIdx >= 0)
      // From some manual testing, taking screenshots and comparing, point sampling with an oversampled atlas seems to work best.
      psout.color = gTextures[cbText.atlasBindIdx].Sample(pointSampler, input.uv);
   else
      psout.color = input.color;

   return psout;
}
