#include "Common.hlsl"
#include "BlobCommon.hlsl"

#define kNumMips 9

struct ComputeCB
{
   int mipId;
   float3 numVoxels;
   float3 worldDim;
   int outTexUAVBindings[kNumMips];
};

ConstantBuffer<ComputeCB> cbCompute : register(b0);
ConstantBuffer<BlobCB> cbBlob : register(b1);

[numthreads(16,16,4)]
void clear(
   int3 id : SV_DispatchThreadID)
{
   gVolumesRW[cbCompute.outTexUAVBindings[cbCompute.mipId]][id] = 1.0f;
}

[numthreads(16,16,4)]
void updateForBlob(
   int3 id : SV_DispatchThreadID)
{
   float3 pos = cbCompute.worldDim * ((float3(id) / cbCompute.numVoxels) - float3(0.5,0.5,0.5));
   /**
   
   M = 4 voxels
   [ -M, M ] -> [ 0, 1 ]
   
   d = distanceToBlob()

   e = clamp(d / voxelSize, -M, M)
   e in [-M, M]

   E = (e + M) / (2M)   in  [ 0, 1 ]

   **/
   // TODO: Optimize
   float voxelSize = (cbCompute.worldDim / cbCompute.numVoxels);
   float range = 4*voxelSize;
   float encodedDist = distanceToBlob(cbBlob, pos) + 0.5;
   encodedDist /= range;

   gVolumesRW[cbCompute.outTexUAVBindings[0]][id] = encodedDist;
}