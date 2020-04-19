
struct MaterialConstantsCB
{
   // TODO: View constants
      float3 camPos;
      float3 camDir;
      float near;
      float far;
      matrix<float, 4, 4> viewProjection;
      matrix<float, 4, 4> objectTransform;
      matrix<float, 4, 4> toWorld;
      // float2 viewSizePx;
      int raytracedShadowIdx;
      int useRaytracedShadows;

   int useFlatColor;
   int diffuseTexIdx;

   float4 albedo;
   float4 specularColor;

   float roughness;
};

struct LightConstantsCB
{
   int numLights;
   float4 positions[EngineKnob_MaxLights];
   float4 colors[EngineKnob_MaxLights];
   float3 intensityBiasFar[EngineKnob_MaxLights];
   int shadowMaps[EngineKnob_MaxLights*6];
};

ConstantBuffer<MaterialConstantsCB> cbShader : register(b1);
