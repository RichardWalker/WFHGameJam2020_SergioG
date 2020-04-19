#ifndef COMMON_HLSL
#define COMMON_HLSL

#define kPi 3.141592654f
#define kInfinite 1e23
#define kMaxStep 40
#define kEpsilon 1e-6

// TODO: Define these from C++!
#define EngineKnob_numGRVDescriptors 512  // g_knobs.numGRVDescriptors
#define EngineKnob_MaxLights 32
#define EngineKnob_MaxEdits 100


struct PSInput
{
   float4 worldPos : Position;
   float4 screenPos : SV_Position;

   float3 normal : Normal;

   float2 uv : Texcoord;
};

struct PSOutput
{
   float4 color : SV_Target0;
   #if defined(WriteDepthInPS)
      float depth : SV_Depth;
   #endif
};

struct PSInputScreen
{
   float4 screenPos : SV_Position;
   float2 uv : Texcoord;
   float4 color : Color;
};

// Not quite sRGB, but good enough for me.

float3
toLinear(float3 x)
{
	return x*x;
}

float3
toGamma(float3 x)
{
	return sqrt(x);
}

// Hash function I found while browsing one of iq's shaders. Played around with the constants to make the noise look noisy...
float3
iqhash( float2 p )
{
  float3 q = float3(
    dot(p, float2(980.1, 411.7)),
    dot(p, float2(269.5, 183.3)),
    dot(p, float2(121.11, 789.42)));

  return frac(sin(q)*1242.192);
}

// http://alex.vlachos.com/graphics/Alex_Vlachos_Advanced_VR_Rendering_GDC2015.pdf
float3
valveDither( float2 pos )
{
   // Iestyn's RGB dither (7 asm instructions) from Portal 2 X360, slightly modified for VR
    float3 dither = dot( float2( 171.0, 231.0 ), pos.xy ).xxx;
    dither.rgb = frac( dither.rgb / float3( 103.0, 71.0, 97.0 ) );

    return 0.375 * dither.rgb / 255.0;
}

Texture2D gTextures[EngineKnob_numGRVDescriptors] : register(t0);
Texture3D gVolumes[EngineKnob_numGRVDescriptors] : register(t0, space1);
TextureCube<float> gCubes[EngineKnob_numGRVDescriptors] : register(t0, space2);

RWTexture2D<float4> gUAV[EngineKnob_numGRVDescriptors] : register(u0);
RWTexture2D<uint> gUAVInt[EngineKnob_numGRVDescriptors] : register(u0);

RWTexture3D<float> gVolumesRW[EngineKnob_numGRVDescriptors] : register(u0);
SamplerState pointSampler : register(s0);
SamplerState linearSampler : register(s1);

#endif // COMMON_HLSL