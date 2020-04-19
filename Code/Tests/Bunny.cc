struct BunnyTest
{
   Platform* p;
   ObjectHandle bunny;
};

void bunnyInit(Platform* p, u8* testMem)
{
   BunnyTest* t = (BunnyTest*)testMem;
   *t = {};

   t->p = p;

   makeAndSetWorld();

   Mesh m = objLoad(p, AssetPath(p, "Bunny.obj"), Lifetime_Frame);
   t->bunny = addMeshToWorld(m);

   defaultTestLightingAndCam();
}

void bunnyTick(u8* testMem, float frameSec)
{
   BunnyTest* t = (BunnyTest*)testMem;

   gpuSetClearColor(1,1,1);

   static float delta = 0.0f;
   u64 now = t->p->getMicroseconds();
   delta += frameSec * 360.0 / 2;

   setTransformForObject(t->bunny, mat4Euler(0,0,DegreeToRadian(delta)));
}

void bunnyDeinit(u8* testMem)
{
   disposeWorld();
}