
__declspec(align(16))
struct ShadowCB
{
   mat4 viewProjection;
   mat4 objectTransform;
   float far;
   vec3 lightPos;
};

__declspec(align(16))
struct LightConstantsCB
{
   int numLights;
   int pad[3];
   vec4 lightPositions[gKnobs.maxLights];
   vec4 lightColors[gKnobs.maxLights];
   struct { float intensity; float bias; float far; float pad[1];} intensityBiasFar[gKnobs.maxLights];
   struct { int value; int pad[3];} shadowMaps[6 * gKnobs.maxLights];  // One for each face for each cube
};

__declspec(align(16))
struct BlobCB
{
   int numEdits;
   int pad[3];

   mat4 transform;

   // xyz center. w radius
   vec4 spheres[gKnobs.maxEdits];
};

__declspec(align(16))
struct SDFReadCB
{
   int texBindIdx;
   vec3 numVoxels;
   vec3 worldDim;
};

__declspec(align(16))
struct PostprocCB
{
   int sourceBindIdx;
   int width;
   int height;
   int time;

   int blackScreen;
   int pad[3];
};

struct WorldRender
{
   Platform* plat;

   RenderMesh* sRenderMeshes;
   Material* sMaterials;
   mat4* sObjectTransforms;

   // PSOs
   PipelineStateHandle blobPSO;
   PipelineStateHandle meshPSO;
   PipelineStateHandle meshPSO_NoCull;
   PipelineStateHandle uiPSO;
   PipelineStateHandle sdfUpdatePSO;
   PipelineStateHandle sdfClearPSO;
   PipelineStateHandle sdfDrawPSO;
   PipelineStateHandle shadowmapPSO;
   PipelineStateHandle postprocPSO;

   MeshRenderHandle screenQuad;

   MaterialHandle* sFreeMaterialHandles;

   // Lighting
   LightConstantsCB lights;
   ResourceHandle lightResource;

   // Color target.
   RenderTarget* rtColor;
   RenderTarget* rtResolved;

   // Shadow maps
   TextureCube shadowCubes[gKnobs.maxLights];
   RenderTarget* shadowMaps[gKnobs.maxLights * 6];  // One for each cube for each face
   RenderTarget* shadowDepth;

   // Raytracing
   PipelineStateHandle dxrPipeline;

   Texture2D dxrOutput;
} * gWorldRender;

u64
worldRenderGlobalSize()
{
   return sizeof(*gWorldRender);
}

void
worldRenderGloalSet(u8* ptr)
{
   gWorldRender = (decltype(gWorldRender)) ptr;
}

struct RayGenCB
{
   mat4 toWorld;
   vec4 origin;
   vec2 resolution;
   int outputBindIdx;
};

void
beginLightingEdit()
{
   WorldRender& r = *gWorldRender;
   r.lights.numLights = 0;
}

LightHandle
addLight()
{
   WorldRender& r = *gWorldRender;

   if (r.lights.numLights == gKnobs.maxLights) {
      Assert(!"Reached max number of lights");
   }

   int idx = r.lights.numLights++;

   LightHandle h = { idx };
   return h;
}

void
setLight(LightHandle h, LightDescription desc)
{
   WorldRender& r = *gWorldRender;

   r.lights.lightPositions[h.idx].rgb = desc.position;
   r.lights.lightPositions[h.idx].a = 1.0f;

   r.lights.lightColors[h.idx].rgb = desc.color;
   r.lights.lightColors[h.idx].a = 1.0f;

   r.lights.intensityBiasFar[h.idx].intensity = desc.intensity;
   r.lights.intensityBiasFar[h.idx].bias = desc.bias;
   r.lights.intensityBiasFar[h.idx].far = desc.far;

   r.lights.shadowMaps[h.idx].value = r.shadowCubes[h.idx].srvCubeBindIndex;
}

void
endLightingEdit()
{
   WorldRender& r = *gWorldRender;

   gpuSetResourceData(r.lightResource, &r.lights, sizeof(r.lights));
}

MaterialHandle
makeMaterial()
{
   WorldRender& r = *gWorldRender;

   Material m = {};
   m.gpuResource = gpuCreateResource(sizeof(MaterialConstantsCB), "Material constants");

   MaterialHandle h;
   if (arrlen(r.sFreeMaterialHandles)) {
      h = arrpop(r.sFreeMaterialHandles);
      r.sMaterials[h.idx] = m;
   }
   else {
      Assert(arrlen(r.sMaterials) < gKnobs.maxMaterials);
      arrput(r.sMaterials, m);
      h.idx = arrlen(r.sMaterials) - 1;
   }

   return h;
}

BLASHandle
getBLAS(ObjectHandle h)
{
   WorldRender& r = *gWorldRender;

   Assert (h.idx < arrlen(r.sRenderMeshes));
   Assert(renderHandleForObject(h)->flags & WorldObject_Mesh);
   MeshRenderHandle rh = renderHandleForObject(h)->mesh;
   RenderMesh* m = r.sRenderMeshes + rh.renderMeshIdx;
   BLASHandle blas = rh.blasHandle;

   return blas;
}

MeshRenderVertex*
packRenderVerts(const Mesh* m, Lifetime life)
{
   size_t numVerts = SBCount(m->sPositions);

   auto* verts = AllocateArray(MeshRenderVertex, numVerts, life);

   for (size_t i = 0; i < numVerts; ++i) {
      verts[i].position = m->sPositions[i];
      verts[i].normal = m->sNormals[i];
      verts[i].texcoord = m->sTexcoords[i];
      verts[i].color = m->sColors[i];
   }

   return verts;
}

MeshRenderHandle
uploadMeshToGPU(MeshRenderVertex* sVerts, u32* sIndices, bool withMaterial, bool withBLAS)
{
   WorldRender& r = *gWorldRender;

   RenderMesh res = {};  // TODO: Store verts in intermediate heap
   res.numIndices = SBCount(sIndices);
   res.vertexBytes = SBCount(sVerts) * sizeof(MeshRenderVertex);
   res.indexBytes = res.numIndices * sizeof(u32);

   res.vertexBuffer = gpuCreateResource(res.vertexBytes, "Vertex Buffer", GPUHeapType_Default, ResourceState_CopyDest);
   res.indexBuffer = gpuCreateResource(res.indexBytes, "Index Buffer", GPUHeapType_Default, ResourceState_CopyDest);

   gpuUploadBuffer(res.vertexBuffer, (u8*)sVerts, res.vertexBytes);
   gpuUploadBuffer(res.indexBuffer, (u8*)sIndices, res.indexBytes);

   gpuBarrierForResource(
      res.vertexBuffer,
      ResourceState_CopyDest,
      ResourceState_VertexBuffer);

   gpuBarrierForResource(
      res.indexBuffer,
      ResourceState_CopyDest,
      ResourceState_IndexBuffer);

   MeshRenderHandle h = {};
   if (withMaterial) {
      h.materialHandle = makeMaterial();
   }
   h.renderMeshIdx = arrlen(r.sRenderMeshes);
   h.transformIdx = arrlen(r.sObjectTransforms);
   if (gKnobs.withRtx && withBLAS) {
      h.blasHandle = gpuMakeBLAS(res.vertexBuffer, res.vertexBytes, sizeof(MeshRenderVertex), res.indexBuffer, res.indexBytes);
   }

   SBPush(r.sRenderMeshes, res, Lifetime_App);
   SBPush(r.sObjectTransforms, mat4Identity(), Lifetime_App);

   return h;
}

MeshRenderHandle
uploadMeshesToGPU(const Mesh* meshes, sz nMeshes, bool withMaterial, bool withBLAS)
{
   WorldRender& r = *gWorldRender;

   RenderMesh res = {};  // TODO: Store verts in intermediate heap

   u64 totalNumVerts = 0;
   u64 totalNumIndices = 0;

   for (sz mi = 0; mi < nMeshes; ++mi) {
      const Mesh& m = meshes[mi];
      totalNumVerts += m.numVerts;
      totalNumIndices += m.numIndices;
   }

   Assert(totalNumVerts);
   Assert(totalNumIndices);

   res.numIndices = totalNumIndices;

   res.vertexBytes = totalNumVerts * sizeof(MeshRenderVertex);
   res.indexBytes = totalNumIndices * sizeof(u32);

   res.vertexBuffer = gpuCreateResource(res.vertexBytes, "Vertex Buffer", GPUHeapType_Default, ResourceState_CopyDest);

   {
      sz offsetBytes = 0;
      for (sz mi = 0; mi < nMeshes; ++mi) {
         const Mesh* m = meshes + mi;
         MeshRenderVertex* renderVerts = packRenderVerts(m, Lifetime_Frame);
         sz nBytes = m->numVerts * sizeof(MeshRenderVertex);
         gpuUploadBufferAtOffset(res.vertexBuffer, (u8*)renderVerts, nBytes, offsetBytes);
         offsetBytes += nBytes;
      }
   }

   gpuBarrierForResource(
      res.vertexBuffer,
      ResourceState_CopyDest,
      ResourceState_VertexBuffer);

   res.indexBuffer = gpuCreateResource(res.indexBytes, "Index Buffer", GPUHeapType_Default, ResourceState_CopyDest);

   u32* indices = {};
   if (nMeshes == 1) {
      indices = meshes[0].sIndices;
   }
   else {
      indices = AllocateArray(u32, totalNumIndices, Lifetime_Frame);
      // Copy, updating indices.
      u32 idxOffset = 0;
      u32 idxi = 0;
      for (sz mi = 0; mi < nMeshes; ++mi) {
         for (sz ii = 0; ii < meshes[mi].numIndices; ++ii) {
            indices[idxi++] = meshes[mi].sIndices[ii] + idxOffset;
         }
         idxOffset += meshes[mi].numVerts;
      }
      Assert (idxi == totalNumIndices);
   }

   gpuUploadBuffer(res.indexBuffer, (u8*)indices, sizeof(u32) * totalNumIndices);

   gpuBarrierForResource(
      res.indexBuffer,
      ResourceState_CopyDest,
      ResourceState_IndexBuffer);

   MeshRenderHandle h = {};
   if (withMaterial) {
      h.materialHandle = makeMaterial();
   }
   h.renderMeshIdx = arrlen(r.sRenderMeshes);
   h.transformIdx = arrlen(r.sObjectTransforms);
   if (gKnobs.withRtx && withBLAS) {
      h.blasHandle = gpuMakeBLAS(res.vertexBuffer, totalNumVerts, sizeof(MeshRenderVertex), res.indexBuffer, totalNumIndices);
   }


   SBPush(r.sRenderMeshes, res, Lifetime_App);
   SBPush(r.sObjectTransforms, mat4Identity(), Lifetime_App);

   // Allocation is a bottleneck when recording commands for shadow maps.
   for (int i = 0; i < gKnobs.maxLights * 6; ++i) {
      char name[1024] = {};
      snprintf(name, ArrayCount(name), "Shadow CB %d for mesh %lld", i, h.renderMeshIdx);
      SBPush(h.sShadowResources, gpuCreateResource(sizeof(ShadowCB), name), Lifetime_App);
   }

   return h;
}

MeshRenderHandle
uploadMeshToGPU(const Mesh& m, bool withMaterial, bool withBLAS)
{
   MeshRenderHandle out = uploadMeshesToGPU(&m, 1, withMaterial, withBLAS);

   return out;
}

static u64
getMeshBuffers(const MeshRenderHandle& h, D3D12_VERTEX_BUFFER_VIEW& outVertexView, D3D12_INDEX_BUFFER_VIEW& outIndexView)
{
   WorldRender& r = *gWorldRender;

   RenderMesh& um = r.sRenderMeshes[h.renderMeshIdx];

   u64 numIndices = um.numIndices;


   return numIndices;
}

u64
setMeshForDraw(MeshRenderHandle rh)
{
   WorldRender& r = *gWorldRender;

   RenderMesh& rm = r.sRenderMeshes[rh.renderMeshIdx];

   gpuSetVertexAndIndexBuffers(rm.vertexBuffer, rm.vertexBytes, rm.indexBuffer, rm.indexBytes);

   return rm.numIndices;
}

BlobRenderHandle
uploadBlobToGPU(const Blob& b)
{
   WorldRender& r = *gWorldRender;

   BlobRenderHandle h = b.renderHandle;
   BlobCB cb = {};
   cb.numEdits = b.numEdits;
   for (int i = 0; i < b.numEdits; ++i) {
      cb.spheres[i].xyz = b.edits[i].center;
      cb.spheres[i].w =  b.edits[i].radius;
   }
   h.transformIdx = arrlen(r.sObjectTransforms);
   SBPush(r.sObjectTransforms, mat4Identity(), Lifetime_App);

   gpuSetResourceData(h.constantResource, &cb, sizeof(cb));

   return h;
}

Material*
getMaterial(MaterialHandle h)
{
   WorldRender& r = *gWorldRender;
   return r.sMaterials + h.idx;
}

void
gpuMarkFreeMaterial(MaterialHandle h, u64 atFrame)
{
   WorldRender& r = *gWorldRender;
   Material* m = r.sMaterials + h.idx;

   gpuMarkFreeResource(m->gpuResource, atFrame);

   arrput(r.sFreeMaterialHandles, h);
}

void
gpuMarkFreeRenderMesh(MeshRenderHandle* h, u64 atFrame)
{
   WorldRender& r = *gWorldRender;

   if (h->materialHandle.idx) {
      gpuMarkFreeMaterial(h->materialHandle, atFrame);
   }

   RenderMesh& m = r.sRenderMeshes[h->renderMeshIdx];

   if (h->blasHandle.idx) {
      gpuMarkFreeBLAS(h->blasHandle, atFrame);
   }
   for (int i = 0; i < arrlen(h->sShadowResources); ++i) {
      ResourceHandle shadowRes = h->sShadowResources[i];
      gpuMarkFreeResource(shadowRes, atFrame);
   }

   gpuMarkFreeResource(m.vertexBuffer, atFrame);
   gpuMarkFreeResource(m.indexBuffer, atFrame);
}

void
shaderGetBytecode(ShaderCode* sc, ShaderId id, u8** outCode, u64* outSz)
{
   ShaderHeader* h = sc->headers + id;
   *outCode = sc->bytes + h->offset;
   *outSz = h->numBytes;
}

PipelineStateHandle
makeGraphicsPipelineState(ShaderCode* sc, ShaderId vs, ShaderId ps, PSOFlags flags)
{
   u8* vsCode;
   u64 szVsCode;
   shaderGetBytecode(sc, vs, &vsCode, &szVsCode);

   u8* psCode;
   u64 szPsCode;
   shaderGetBytecode(sc, ps, &psCode, &szPsCode);

   PipelineStateHandle pso = gpuGraphicsPipelineState(vsCode, szVsCode, psCode, szPsCode, flags);
   return pso;
}

PipelineStateHandle
makeComputePipelineState(ShaderCode* sc, ShaderId id)
{
   u8* code;
   u64 szCode;
   shaderGetBytecode(sc, id, &code, &szCode);

   PipelineStateHandle pso = gpuComputePipelineState(code, szCode);
   return pso;
}

void
setupPipelineStates(ShaderCode* shaderCode)
{
   WorldRender& r = *gWorldRender;

   PSOFlags MSflag = (gKnobs.useMultisampling) ? PSOFlags_MSAA : PSOFlags_None;

   r.shadowmapPSO = makeGraphicsPipelineState(shaderCode, Shader_ShadowmapVS, Shader_ShadowmapPS, PSOFlags(PSOFlags_NoCull | PSOFlags_R8FloatTarget));
   r.postprocPSO = makeGraphicsPipelineState(shaderCode, Shader_PostprocVS, Shader_PostprocPS, PSOFlags(PSOFlags_NoCull | PSOFlags_NoDepth));

   // TODO: This will probably get hairy. We might need some PSO variation system.
   r.meshPSO = makeGraphicsPipelineState(shaderCode, Shader_MeshVS, Shader_MeshPS, MSflag);
   r.meshPSO_NoCull = makeGraphicsPipelineState(shaderCode, Shader_MeshVS, Shader_MeshPS, PSOFlags(MSflag | PSOFlags_NoCull));

   r.uiPSO = makeGraphicsPipelineState(shaderCode, Shader_UIVS, Shader_UIPS, PSOFlags(PSOFlags_NoDepth | PSOFlags_AlphaBlend));
   r.blobPSO = makeGraphicsPipelineState(shaderCode, Shader_SDFRaymarchVS, Shader_SDFRaymarchPS, PSOFlags_None);
   r.sdfClearPSO = makeComputePipelineState(shaderCode, Shader_SDFComputeClear);
   r.sdfUpdatePSO = makeComputePipelineState(shaderCode, Shader_SDFComputeUpdate);

   if (gKnobs.withRtx) {
      RaytracingPSODesc desc = {};

      ShaderHeader h = shaderCode->headers[Shader_Raytracing];

      desc.byteCode = (u8*)shaderCode->bytes + h.offset;
      desc.szByteCode = h.numBytes;

      // Hit group 0
      {
         RaytracingHitgroup hitgroup = {};
         hitgroup.name = L"CastHitGroup";
         hitgroup.closestHit = L"TraceShadowRays";
         SBPush(desc.sHitGroups, hitgroup, Lifetime_Frame);
      }

      // Hit group 1
      {
         RaytracingHitgroup hitgroup = {};
         hitgroup.name = L"ShadowHitGroup";
         hitgroup.closestHit = L"ShadowHit";
         SBPush(desc.sHitGroups, hitgroup, Lifetime_Frame);
      }

      SBPush(desc.sMissIds, L"Miss", Lifetime_Frame);  // Miss 0
      SBPush(desc.sMissIds, L"ShadowMiss", Lifetime_Frame);  // Miss 1
      r.dxrPipeline = gpuCreateRaytracingPipeline(desc);
   }
}

void
worldRenderInit(Platform* plat)
{
   WorldRender& r = *gWorldRender;

   r.plat = plat;

   // Sentinel material
   {
      MaterialHandle h = makeMaterial();
      Assert( h.idx == 0 );
      r.sMaterials[h.idx].constants.useFlatColor = 1;
      r.sMaterials[h.idx].constants.albedo = { 1.0, 0.0, 1.0, 1.0f };
   }

   // MS render targets.

   {
      RTFlags ms = (gKnobs.useMultisampling) ? RTFlags_Multisampled : RTFlags_None;

      r.rtColor = gpuCreateColorTarget(gpu()->fbWidth, gpu()->fbHeight, ResourceState_ResolveSource, ms);
      if (gKnobs.useMultisampling) {
         r.rtResolved = gpuCreateColorTarget(gpu()->fbWidth, gpu()->fbHeight, ResourceState_ResolveSource, RTFlags_WithSRV);
      }
   }

   // Shadow maps
   for (int i = 0; i < gKnobs.maxLights; ++i) {
      r.shadowCubes[i] = gpuCreateTextureCube(gKnobs.shadowResolution, gKnobs.shadowResolution, TextureFormat::R32_FLOAT, ResourceState_RenderTarget);

      for (int f = 0; f < 6; ++f) {
         r.shadowMaps[i*6 + f] = gpuCreateColorTargetForCubeFace(r.shadowCubes + i, gKnobs.shadowResolution, gKnobs.shadowResolution, CubeFace(f));
      }
   }
   r.shadowDepth = gpuCreateDepthTarget(gKnobs.shadowResolution, gKnobs.shadowResolution, RTFlags(RTFlags_None));

   if (gKnobs.withRtx) {
      r.dxrOutput = gpuCreateTexture2D(gpu()->fbWidth, gpu()->fbHeight, true, TextureFormat::R32_UINT);
   }

   r.lightResource = gpuCreateResource(AlignPow2(sizeof(LightConstantsCB), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), "Light constants");

   // Screen quad
   r.screenQuad = uploadMeshToGPU(makeQuad(1.0, 0.0, Lifetime_World), /*bool withMaterial*/false, /*bool withBLAS*/false);
}

void
wrResize(int w, int h)
{
   WorldRender* r = gWorldRender;
   gpuMarkFreeColorTarget(r->rtColor, gpu()->frameCount);
   if (gKnobs.useMultisampling) {
      gpuMarkFreeColorTarget(r->rtResolved, gpu()->frameCount);
   }

   RTFlags ms = (gKnobs.useMultisampling) ? RTFlags_Multisampled : RTFlags_None;

   r->rtColor = gpuCreateColorTarget(w, h, ResourceState_ResolveSource, ms);
   if (gKnobs.useMultisampling) {
      r->rtResolved = gpuCreateColorTarget(w, h, ResourceState_ResolveSource, RTFlags_WithSRV);
   }
}

void
wrDispose()
{
   WorldRender* wr = gWorldRender;
   for (int i = 0; i < gKnobs.maxLights; ++i) {
      gpuMarkFreeResource(wr->shadowCubes[i].resourceHandle, gpu()->frameCount);
   }
   gpuMarkFreeDepthTarget(wr->shadowDepth, gpu()->frameCount);
}

Material*
materialForObject(ObjectHandle h)
{
   WorldObjectRenderHandle rh = getWorld()->renderHandles[h.idx];

   MaterialHandle mh =
      (rh.flags & WorldObject_Mesh) ? rh.mesh.materialHandle :
      rh.blob.materialHandle;

   Material* m = gWorldRender->sMaterials + mh.idx;
   return m;
}

void
materialSetFlag(Material* mat, PSOFlags flag, bool set)
{
   int v = mat->psoFlags;
   if (set) {
      v |= flag;
   } else {
      v &= flag;
   }

   mat->psoFlags = PSOFlags(v);
}

bool
shouldDoRaytracing()
{
   return gKnobs.useRaytracedShadows && gKnobs.withRtx;
}

void
renderWorld()
{
   WorldRender* wr = gWorldRender;

   if (gKnobs.useMultisampling) {
   }
   else {
      gpuBarrierForResource(
         gpuRenderTargetResource(gpuBackbuffer()),
         ResourceState_Present,
         ResourceState_RenderTarget);
   }

   // Shadows
   if (getWorld()) {
      Camera* cam = &getWorld()->cam;
      // Fill shadow buffer
      if (shouldDoRaytracing()) {
         logMsg("Building acceleration structure for frame %d\n", gpu()->frameCount);
         // Rebuild acceleration structure.
         TLAS* tlas = gpuCreateTLAS(gKnobs.maxObjects);

         ObjectIterator* iter = objectIterateBegin((WorldObjectFlag)(WorldObject_Mesh | WorldObject_Visible | WorldObject_CastsShadows));
         while (objectIterateHasNext(iter)) {
            ObjectHandle h = objectIterateNext(iter);
            gpuAppendToTLAS(tlas, getBLAS(h), transformForObject(h));
         }
         objectIterateEnd(iter);
         gpuBuildTLAS(tlas);

         gpuSetRaytracingPipeline(wr->dxrPipeline);

         mat4 lookatMat = mat4Lookat(cam->eye, cam->lookat, cam->up);
         mat4 persp = mat4Persp(cam, 1.0);
         mat4 viewProjection = persp * lookatMat;

         // Create CBV for ray tracing
         u64 size = AlignPow2(sizeof(RayGenCB), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
         ResourceHandle dxrCBRes = gpuCreateResource(size, "Raygen CB");
         gpuMarkFreeResource(dxrCBRes, gpu()->frameCount);

         RayGenCB cb = {};

         cb.origin.xyz = cam->eye;
         cb.toWorld = mat4Inverse(viewProjection);
         cb.resolution = vec2{(float)gpu()->fbWidth, (float)gpu()->fbHeight};
         cb.outputBindIdx = wr->dxrOutput.uavBindIndex;

         gpuSetResourceData(dxrCBRes, &cb, sizeof(cb));
         gpuSetRaytracingConstantSlot(0, dxrCBRes, sizeof(cb));
         gpuSetRaytracingConstantSlot(1, wr->lightResource, sizeof(LightConstantsCB));

         gpuDispatchRays();

         gpuUAVBarrier(wr->dxrOutput.resourceHandle);
      }
      // Fill shadow maps.
      else {
         gpuSetPipelineState(wr->shadowmapPSO);

         gpuBeginMarker("Shadow Maps");

         for (int lightIdx = 0; lightIdx < wr->lights.numLights; ++lightIdx) {
            // For each side...
            // TODO: Use a geometry shader instead of doing 6 draw calls.

            vec3 faceDir[6] = {
               vec3{1,0,0}, // PosX
               vec3{-1,0,0}, // NegX
               vec3{0,1, 0}, // PosY
               vec3{0,-1,0}, // NegY
               vec3{0,0,1}, // PosZ
               vec3{0,0,-1}, // NegZ
            };
            vec3 faceUp[6] = {
               vec3{0,1,0},
               vec3{0,1,0},
               vec3{0,0,-1},
               vec3{0,0,1},
               vec3{0,1,0},
               vec3{0,1,0},
            };

            char* markers[6] = {
               "CubeFace_PosX",
               "CubeFace_NegX",
               "CubeFace_PosY",
               "CubeFace_NegY",
               "CubeFace_PosZ",
               "CubeFace_NegZ",
            };

            gpuSetViewport(0, 0, gKnobs.shadowResolution, gKnobs.shadowResolution);
            for (int f = CubeFace_PosX; f <= CubeFace_NegZ; ++f) {
               gpuBeginMarker(markers[f]);

               vec3 old = gpuSetClearColor(MaxFloat, MaxFloat, MaxFloat);

               gpuSetRenderTargets(wr->shadowMaps[lightIdx * 6 + f], wr->shadowDepth);
               gpuClearRenderTargets(wr->shadowMaps[lightIdx * 6 + f], wr->shadowDepth);

               gpuSetClearColor(old.r, old.g, old.b);

               Camera lc = {};
               lc.near = wr->lights.intensityBiasFar[lightIdx].bias;  // Does it make sense to use the bias as the near plane?
               lc.far = wr->lights.intensityBiasFar[lightIdx].far;
               lc.fov = DegreeToRadian(90);
               lc.eye = wr->lights.lightPositions[lightIdx].xyz;
               lc.lookat = lc.eye + faceDir[f];
               lc.up = faceUp[f];

               mat4 lookatMat = mat4Lookat(lc.eye, lc.lookat, lc.up);
               mat4 persp = mat4Persp(&lc, 1.0f);
               mat4 viewProj = persp * lookatMat;

               // TODO: Move to use a geometry shader?
               // TODO: Cull objects by visibility to light.

               ObjectIterator* iter = objectIterateBegin((WorldObjectFlag)(WorldObject_Mesh | WorldObject_Visible | WorldObject_CastsShadows));
               while (objectIterateHasNext(iter)) {
                  ObjectHandle h = objectIterateNext(iter);
                  MeshRenderHandle rh = renderHandleForObject(h)->mesh;

                  mat4 objViewProjection = viewProj * wr->sObjectTransforms[rh.transformIdx];

                  ShadowCB cb;
                  cb.viewProjection = viewProj;
                  cb.objectTransform = wr->sObjectTransforms[rh.transformIdx];
                  cb.far = wr->lights.intensityBiasFar[lightIdx].far;
                  cb.lightPos = wr->lights.lightPositions[lightIdx].xyz;

                  ResourceHandle cbRes = rh.sShadowResources[lightIdx*6 + f];
                  gpuSetResourceData(cbRes, &cb, sizeof(cb));
                  gpuSetGraphicsConstantSlot(0, cbRes);

                  u64 numIndices = setMeshForDraw(rh);
                  gpuDrawIndexed(numIndices);
               }
               objectIterateEnd(iter);
               gpuEndMarker();  // Face marker
            }
            gpuBarrierForResource(
               wr->shadowCubes[lightIdx].resourceHandle,
               ResourceState_RenderTarget,
               ResourceState_PixelShaderResource);
         }
         gpuEndMarker();  // Shadow maps
      }
   }

   // Clear render target.
   gpuSetViewport(0,0, gpu()->fbWidth, gpu()->fbHeight);
   gpuClearRenderTargets(wr->rtColor, gpuDefaultDepthTarget());
   gpuSetRenderTargets(wr->rtColor, gpuDefaultDepthTarget());

   // Lighting
   if (getWorld()) {
      gpuBeginMarker("Lighting");

      gpuSetGraphicsConstantSlot(2, wr->lightResource);

      Camera* cam = &getWorld()->cam;

      mat4 lookatMat = mat4Lookat(cam->eye, cam->lookat, cam->up);
      mat4 persp = mat4Persp(cam, (float)gpu()->fbWidth / gpu()->fbHeight);
      mat4 viewProjection = persp * lookatMat;

      ObjectIterator* iter = objectIterateBegin((WorldObjectFlag)(WorldObject_Mesh | WorldObject_Visible));
      while (objectIterateHasNext(iter)) {
         ObjectHandle h = objectIterateNext(iter);
         {
            MeshRenderHandle rh = renderHandleForObject(h)->mesh;
            Material* m = getMaterial(rh.materialHandle);

            PSOFlags MSflag = (gKnobs.useMultisampling) ? PSOFlags_MSAA : PSOFlags_None;

            // Note that we test for equality, not bitwise and.
            if (m->psoFlags == PSOFlags(MSflag)) {
               gpuSetPipelineState(wr->meshPSO);
            }
            else if (m->psoFlags == PSOFlags(MSflag | PSOFlags_NoCull)) {
               gpuSetPipelineState(wr->meshPSO_NoCull);
            } else {
               Assert(false);  // No PSO for this combination of flags.
            }

            // Set-up mesh.
            u64 numIndices = setMeshForDraw(rh);

            // Set constant buffer
            MaterialConstantsCB& buffer = m->constants;
            {
               buffer.camPos = cam->eye;
               buffer.camDir = normalized(cam->lookat - cam->eye);
               buffer.far = cam->far;
               buffer.near = cam->near;
               // Set up lookat matrix
               static float rotation = 0.0f;
               buffer.objectTransform = wr->sObjectTransforms[rh.transformIdx];
               buffer.viewProjection = viewProjection;
               buffer.raytracedShadowIdx = wr->dxrOutput.uavBindIndex;
               buffer.useRaytracedShadows = objectTestFlag(h, WorldObject_CastsShadows) ?  shouldDoRaytracing() : false;
            }
            gpuSetResourceData(m->gpuResource, &buffer, sizeof(buffer));

            gpuSetGraphicsConstantSlot(1, m->gpuResource);

            gpuDrawIndexed(numIndices);
         }
      }
      objectIterateEnd(iter);

      if (!shouldDoRaytracing()) {
         for (int i = 0; i < wr->lights.numLights; ++i) {
            gpuBarrierForResource(
               wr->shadowCubes[i].resourceHandle,
               ResourceState_PixelShaderResource,
               ResourceState_RenderTarget);
         }
      }

      gpuEndMarker();
   }

   // Resolve multisampled target.
   if (gKnobs.useMultisampling) {
      gpuBarrierForResource(
         gpuRenderTargetResource(wr->rtColor),
         ResourceState_RenderTarget,
         ResourceState_ResolveSource);
      gpuBarrierForResource(
         gpuRenderTargetResource(wr->rtResolved),
         ResourceState_PixelShaderResource,
         ResourceState_ResolveDest);

      gpuResolve(wr->rtColor, wr->rtResolved);

      gpuBarrierForResource(
         gpuRenderTargetResource(wr->rtColor),
         ResourceState_ResolveSource,
         ResourceState_RenderTarget);  // UI pass will render after.

      gpuBarrierForResource(
         gpuRenderTargetResource(wr->rtResolved),
         ResourceState_ResolveDest,
         ResourceState_PixelShaderResource);

   }

   // Postproc
   {
      PostprocCB cb = {};

      cb.sourceBindIdx = gpuRenderTargetBindIndex(wr->rtResolved);
      cb.width = gpu()->fbWidth;
      cb.height = gpu()->fbHeight;
      cb.time = wr->plat->getMicroseconds();
      cb.blackScreen = gGameJamBlackScreen;

      ResourceHandle res = gpuCreateResource(sizeof(cb), "Postproc resource");
      gpuMarkFreeResource(res, gpu()->frameCount);

      gpuSetResourceData(res, &cb, sizeof(cb));
      gpuSetGraphicsConstantSlot(0, res);

      gpuSetRenderTargets(gpuBackbuffer(), nullptr);

      gpuSetPipelineState(wr->postprocPSO);
      u64 numIndices = setMeshForDraw(wr->screenQuad);
      gpuDrawIndexed(numIndices);
   }

}
