
struct TextConstants
{
   mat4 transform;

   int atlasBindIdx;
   int pad0[3];
};


void
fontDeinit(Font* t)
{
   gpuMarkFreeRenderMesh(&t->rmesh, gpu()->frameCount);
}

void
fontInit(Platform* plat, Font* t, char* fontPath, float* customSizes)
{
   t->atlasSize = 2048; //gKnobs.height;

   t->atlasMesh = makeQuad(2048, 0.0, Lifetime_Frame);
   t->rmesh = uploadMeshToGPU(t->atlasMesh, /*material*/false, /*blas*/false);

   // Load atlas
   u8* data = {};
   u64 bytes = plat->fileContentsAscii(fontPath, data, Lifetime_App);

   if (!bytes) {
      Assert(false);  // COuld not find font
   }
   else {
      stbtt_fontinfo fontInfo = {};
      int success = stbtt_InitFont(&fontInfo, data, 0);
      if (!success) {
         Assert(false);  // Invalid font
      }
      else {
         // Pack the font
         stbtt_pack_context spc = {};

         int width = t->atlasSize;
         int height = t->atlasSize;

         if (!customSizes) {
            t->fontSizesPx[FontSize_Tiny] = 10;
            t->fontSizesPx[FontSize_Small] = 12;
            t->fontSizesPx[FontSize_Medium] = 14;
            t->fontSizesPx[FontSize_Big] = 18;
         }
         else {
            for (int i = 0; i < FontSize_Count; ++i) {
               t->fontSizesPx[i] = customSizes[i];
            }
         }

         t->numChars = 127;  // ASCII range for now.

         t->packedData[FontSize_Tiny] = AllocateArray(stbtt_packedchar, t->numChars, Lifetime_App);
         t->packedData[FontSize_Small] = AllocateArray(stbtt_packedchar, t->numChars, Lifetime_App);
         t->packedData[FontSize_Medium] = AllocateArray(stbtt_packedchar, t->numChars, Lifetime_App);
         t->packedData[FontSize_Big] = AllocateArray(stbtt_packedchar, t->numChars, Lifetime_App);

         unsigned char* atlas = (unsigned char*)allocateBytes(width * height, Lifetime_App);

         int packOk = stbtt_PackBegin(&spc, atlas, width, height, 0, 1, {});
         if (!packOk) {
            Assert(false); // Packing failed
         }
         else {
            stbtt_PackSetOversampling(&spc, gKnobs.fontOversampling, gKnobs.fontOversampling);
            for (int i = 0; i < FontSize_Count; ++i) {
               packOk &= stbtt_PackFontRange(
                  &spc,
                  data,
                  /*fontIndex*/0,
                  STBTT_POINT_SIZE(t->fontSizesPx[i]),
                  0, t->numChars /*ascii range*/,
                  (stbtt_packedchar*)t->packedData[i]);
            }

            if (!packOk) {
               Assert(false); // Failed to pack font range
            }
            else {
               stbtt_PackEnd(&spc);
               t->atlasTexture = gpuUploadTexture2D(width, height, 1, atlas);
            }
         }
      }
   }
}

void
buildLineOfText(Font& t, FontSize size, char* contents, MeshRenderHandle& outMesh, ResourceHandle& outResHnd)
{
   Assert(contents && strlen(contents));
   sz szContents = strlen(contents);

   MeshRenderVertex* sRenderVerts = NULL;
   u32* sIndices = NULL;

   auto* quads = AllocateArray(stbtt_aligned_quad, szContents, Lifetime_Frame);

   // Building at 0,0. We can translate on draw.
   float cursorX = 0;
   float cursorY = 0;

   outResHnd = gpuCreateResource(sizeof(TextConstants), "Text constants");

   for (sz i = 0; i < szContents; ++i) {
      stbtt_GetPackedQuad((stbtt_packedchar*)t.packedData[size],
                           t.atlasSize, t.atlasSize,
                           contents[i],
                           &cursorX, &cursorY,
                           quads + i,
                           /*int align_to_integer*/ 0);
      // TODO: If we wrap it would probably be here?

      // Add quad to mesh
      vec3 n = vec3{0, 0, -1};
      /**
         s0,t1         s1,t1
         *-------------*
         v3            v2
         |             |
         |             |
         | s0,t0       |
         *-------------*  s1, t0
         v0            v1
      **/
      MeshRenderVertex v0 = {};
      MeshRenderVertex v1 = {};
      MeshRenderVertex v2 = {};
      MeshRenderVertex v3 = {};

      v0.position.x = quads[i].x0;
      v0.position.y = quads[i].y0;
      v0.texcoord.u = quads[i].s0;
      v0.texcoord.v = quads[i].t0;

      v1.position.x = quads[i].x1;
      v1.position.y = quads[i].y0;
      v1.texcoord.u = quads[i].s1;
      v1.texcoord.v = quads[i].t0;

      v2.position.x = quads[i].x1;
      v2.position.y = quads[i].y1;
      v2.texcoord.u = quads[i].s1;
      v2.texcoord.v = quads[i].t1;

      v3.position.x = quads[i].x0;
      v3.position.y = quads[i].y1;
      v3.texcoord.u = quads[i].s0;
      v3.texcoord.v = quads[i].t1;

      // Set all the normals, z and w
      v0.normal = v1.normal = v2.normal = v3.normal = n;
      v0.position.z = v1.position.z = v2.position.z = v3.position.z = 0;
      v0.position.w = v1.position.w = v2.position.w = v3.position.w = 1;

      u32 v0i = SBCount(sRenderVerts); SBPush(sRenderVerts, v0, Lifetime_Frame);
      u32 v1i = SBCount(sRenderVerts); SBPush(sRenderVerts, v1, Lifetime_Frame);
      u32 v2i = SBCount(sRenderVerts); SBPush(sRenderVerts, v2, Lifetime_Frame);
      u32 v3i = SBCount(sRenderVerts); SBPush(sRenderVerts, v3, Lifetime_Frame);

      // Rects passed as CCW because coordinates will get flipped by the UI shader, reversing the winding order.
      SBPush(sIndices, v0i, Lifetime_Frame);
      SBPush(sIndices, v1i, Lifetime_Frame);
      SBPush(sIndices, v2i, Lifetime_Frame);

      SBPush(sIndices, v2i, Lifetime_Frame);
      SBPush(sIndices, v3i, Lifetime_Frame);
      SBPush(sIndices, v0i, Lifetime_Frame);
   }

   outMesh = uploadMeshToGPU(sRenderVerts, sIndices, /*withMaterial*/false, /*blas*/false);
}

void
renderUIElem(Font& t, const MeshRenderHandle& rmesh, ResourceHandle resHnd, const mat4& transform = mat4Identity(), bool flatColor = false)
{
   gpuSetPipelineState(gWorldRender->uiPSO);  // TODO: uiPSO should not live in worldrender

   u64 numIndices = setMeshForDraw(rmesh);

   TextConstants consts = {};

   mat4 textOrtho = mat4Identity();

   textOrtho[0][0] = 2.0f / gpu()->fbWidth;
   textOrtho[1][1] = -2.0f / gpu()->fbHeight;
   textOrtho[3][0] = -1;
   textOrtho[3][1] = 1;

   if (!flatColor) {
      consts.atlasBindIdx = t.atlasTexture.srvBindIndex;
   }
   else {
      consts.atlasBindIdx = -1;
   }
   consts.transform = textOrtho * transform;

   gpuSetResourceData(resHnd, &consts, sizeof(consts));
   gpuSetGraphicsConstantSlot(0, resHnd);
   gpuDrawIndexed(numIndices);
}

void
renderText(Font& t, const MeshRenderHandle& rmesh, ResourceHandle resHnd, const mat4& transform = mat4Identity())
{
   renderUIElem(t, rmesh, resHnd, transform, false);
}

void
renderWidgets(Font& t, const MeshRenderHandle& rmesh, ResourceHandle resHnd, const mat4& transform = mat4Identity())
{
   renderUIElem(t, rmesh, resHnd, transform, true);
}

