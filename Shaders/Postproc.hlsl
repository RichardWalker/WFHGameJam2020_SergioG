#include "Common.hlsl"

struct PostprocCB
{
   int sourceBindIdx;
   int width;
   int height;
   int time;
   int blackScreen;
};

ConstantBuffer<PostprocCB> cb : register(b0);

PSInput
vertexMain(float4 position : POSITION, float2 texcoord : TEXCOORD, float3 normal : NORMAL)
{
   PSInput result;

   result.screenPos = position;
   result.uv = texcoord;

   return result;
}


PSOutput
pixelMain(PSInput input)
{
   PSOutput output;
   output.color = float4(input.uv,1,1);
   float2 screen = float2(cb.width, cb.height);

   if (cb.blackScreen) {
      output.color = float4(0,0,0,1);
      return output;
   }

   float2 sampleUV = input.uv;

   // Voronoi fuzz
#if 1
   // float cellSizePx = 6;
   // int2 numCells = float2(cb.width / cellSizePx, cb.height / cellSizePx);
   int2 numCells = 400;
   float2 cellSizePx = screen / numCells;

   float2 maxCellSize = screen / numCells; //float2(cb.width / (float)numCells.x, cb.height / (float)numCells);

   sampleUV *= numCells;

   float2 voronoiSample = sampleUV;
   float d = maxCellSize;

   for (int cellOffY = -1; cellOffY < 1; ++cellOffY) {
      for (int cellOffX = -1; cellOffX < 1; ++cellOffX) {
         float2 cellIdx = floor(sampleUV) + float2(cellOffX, cellOffY);
         cellIdx.x += cellIdx.y % 2 * 0.5;

         float2 randomOff = (0.5 * iqhash(cellIdx + cb.time / 300000)) * maxCellSize;

         float t = ((float)(cb.time) / 1000000) % 2*kPi;
         float2 cellCenter = cellIdx * maxCellSize + maxCellSize/2 + randomOff + maxCellSize*0.1*float2(sin(t), cos(t));
         cellCenter /= screen;

         float ld = length(cellCenter - input.uv);
         if (ld < d) {
            d = ld;
            voronoiSample = cellCenter;
         }
      }
   }

   sampleUV = voronoiSample;

   // Add a bit of noise to make it more painterly
   sampleUV += iqhash(input.uv) * 1/screen * (cellSizePx * 0.5);


#endif

   // Simple downsample
#if 0
   sampleUV *= float2(320, 320);

   sampleUV = floor(sampleUV);

   sampleUV /= float2(320, 320);
#endif

   float2 pxsz = float2(1.0 / cb.width, 1.0 / cb.height);

   // Chromatic aberration
#if 1
   float caWidth = 5 * cb.width / 1920.0;

   caWidth *= length(input.uv - float2(0.5,0.5));

   output.color.g = gTextures[cb.sourceBindIdx].SampleLevel(pointSampler, sampleUV, 0).g;
   output.color.r = gTextures[cb.sourceBindIdx].SampleLevel(pointSampler, sampleUV + caWidth*float2(pxsz.x, 0), 0).r;
   output.color.b = gTextures[cb.sourceBindIdx].SampleLevel(pointSampler, sampleUV + -caWidth*float2(pxsz.x, 0), 0).b;
#else
   output.color.rgb = gTextures[cb.sourceBindIdx].SampleLevel(pointSampler, sampleUV, 0).rgb;
#endif

#if 1
   // Add a little bit of visible noise.
   float noiseLevel = 20 / 255.0;

   float i = (cb.time % 10000) / 10000.0;
   output.color.rgb += iqhash(input.uv + float2(i,i)) * noiseLevel;
   output.color.rgb -= noiseLevel / 2;
#endif

   return output;
}