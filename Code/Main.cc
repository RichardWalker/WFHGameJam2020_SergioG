
AppInitCallbackProcDef(appInit)
{
   memInit();

   logInit(plat);

   gpuInit(plat, width, height);
   worldRenderInit(plat);

   audioInit();
   makeAndSetWorld();

   fontInit(plat, &editor()->defaultFont, "C:/windows/fonts/arial.ttf");
   immInit(&editor()->defaultFont);

   if (plat->runMode == RunMode_Game) {
      gameInit(plat);
   }
   else if (plat->runMode == RunMode_Tests) {
      testsInit(plat);
   }

   gpuFinishInit();

   if (fullscreen) {
      gpuGoFullscreen();
   }
}

AppTickCallbackProcDef(appTick)
{
   // Reset command lists.
   gpuPrepareForMainLoop();

   // Check for resize.
   {
      int w,h;
      if (getResizeInput(plat, &w, &h)) {
         gpuResize(w, h);
         wrResize(w, h);
      }
   }
   immNewFrame();

   MouseInput* m = getMouseInput(plat);

   if (m->numMouseMove) {
      int x = m->mouseMove[m->numMouseMove - 1].x;
      int y = m->mouseMove[m->numMouseMove - 1].y;
      immMouseMove(x, y);
   }
   if (m->numClicks) {
      MouseClick left = m->clicks[m->numClicks - 1].left;
      if (left == MouseDown) {
         immClick();
      }
      else if (left == MouseUp) {
         immRelease();
      }
   }
   if (plat->runMode == RunMode_Game) {
      gameTick();
   }
   else if (plat->runMode == RunMode_Tests) {
      testsTick();
   }
   audioFrameEnd();
   // Render
   {
      gpuBeginRenderTick();

      renderWorld();
      immRender();

      gpuEndRenderTick();
   }

   freePages(Lifetime_Frame);
}

AppLoadShadersProcDef(appLoadShaders)
{
   setupPipelineStates(shaderCode);
}

GetGlobalSizeTableProcDef(getGlobalSizeTable)
{
   sizes[GlobalTable_Logging] = logGlobalSize();
   sizes[GlobalTable_Render] = gpuGlobalSize();
   sizes[GlobalTable_UI] = immGlobalSize();
   sizes[GlobalTable_Memory] = memoryGlobalsSize();
   sizes[GlobalTable_Audio] = audioGlobalSize();
   sizes[GlobalTable_Editor] = editorGlobalSize();
   sizes[GlobalTable_Gameplay] = gameGlobalSize();
   sizes[GlobalTable_Tests] = testGlobalSize();
   sizes[GlobalTable_WorldRender] = worldRenderGlobalSize();
}

PatchGlobalTableProcDef(patchGlobalTable)
{
   gpuGlobalSet(pointers[GlobalTable_Render]);
   immGlobalSet(pointers[GlobalTable_UI]);
   memoryGlobalsSet(pointers[GlobalTable_Memory]);
   audioGlobalSet(pointers[GlobalTable_Audio]);
   editorGlobalSet(pointers[GlobalTable_Editor]);
   gameGlobalSet(pointers[GlobalTable_Gameplay]);
   worldRenderGloalSet(pointers[GlobalTable_WorldRender]);
   testGlobalSet(pointers[GlobalTable_Tests]);
   logGlobalSet(pointers[GlobalTable_Logging]);
}

AppDisposeProcDef(appDispose)
{
   fontDeinit(&editor()->defaultFont);
   disposeWorld();
   wrDispose();
   gpuDispose();
   freePages(Lifetime_App); // TODO: Not really necessary. Disable for release?
}