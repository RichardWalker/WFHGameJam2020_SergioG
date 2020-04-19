struct BlobCB
{
   int numEdits;

   float4x4 transform;
   // Only spheres for now
   float4 spheres[EngineKnob_MaxEdits];
};

float
sMin(float a, float b, float k)
{
    float h = max( k-abs(a-b), 0.0 )/k;
    return min( a, b ) - h*h*k*(1.0/4.0);
}

float
distanceToBlob(BlobCB blob, float3 p)
{
   float d = kInfinite;

   for (int editIdx = 0; editIdx < blob.numEdits; ++editIdx) {
      float3 center = blob.spheres[editIdx].xyz;
      float r = blob.spheres[editIdx].w;
      d = sMin(d, length(p - center) - r, 0.1);
   }

   return d;
}

float3
blobNormal(BlobCB blob, float3 p)
{
   float2 h = float2(0.0001, 0);
   float3 n = normalize(float3(
      distanceToBlob(blob, p + h.xyy),
      distanceToBlob(blob, p + h.yxy),
      distanceToBlob(blob, p + h.yyx)));
   return n;
}
