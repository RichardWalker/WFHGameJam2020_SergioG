struct PLTest
{
   Platform* p;

   ObjectHandle objs[4];
};

void pltestInit(Platform* p, u8* testMem)
{
   PLTest* t = (PLTest*)testMem;
   *t = {};

   t->p = p;

   makeAndSetWorld();

   // Room
   if (1)
   {
      Mesh m = objLoad(p, AssetPath(p, "UnitCube.obj"), Lifetime_Frame);
      // Mesh m = objLoad(p, AssetPath(p, "UnitSphere.obj"), Lifetime_Frame);

      ObjectHandle h = addMeshToWorld(m);
      Material* mat = materialForObject(h);

      materialSetFlag(mat, PSOFlags_NoCull, true);
      objectSetFlag(h, WorldObject_CastsShadows, true);

      setTransformForObject(
         h,
         mat4Scale(4));
   }

   // Objects
   {
      Mesh m = objLoad(p, AssetPath(p, "UnitSphere.obj"), Lifetime_Frame);
      // Mesh m = objLoad(p, AssetPath(p, "UnitCube.obj"), Lifetime_Frame);

      t->objs[0] = addMeshToWorld(m);
      t->objs[1] = addMeshToWorld(m);
      t->objs[2] = addMeshToWorld(m);
      // t->objs[3] = addMeshToWorld(m);
   }

   // Set camera
   getWorld()->cam.eye = {0, 0, 3.5};
   getWorld()->cam.lookat = {0, 0, 0};
   getWorld()->cam.near = 0.01;
   getWorld()->cam.far = 50;
   getWorld()->cam.fov = DegreeToRadian(90.0);
}

void pltestTick(u8* testMem, float frameSec)
{
   PLTest* t = (PLTest*)testMem;

   static float angle = 0.0f;
   angle += frameSec * DegreeToRadian(360.0 / 4);

   // objectSetFlag(t->obj, WorldObject_CastsShadows, true);

   // Material* mat = materialForObject(t->sphere);

   beginLightingEdit();

   #if 1
   // "Random"
   // setTransformForObject(
   //    t->sphere,
   //    mat4Translate(cos(angle),sin(angle * 0.333), -cos(angle*0.5) - 2) * mat4Scale(0.5));

   // Positive x
   // setTransformForObject(
   //    t->sphere,
   //    mat4Translate(2,sin(angle * 0.333),0) * mat4Scale(0.5));

   // Negative x
   setTransformForObject(
      t->objs[0],
      mat4Translate(2,0,sin(angle)) * mat4Scale(1));
   // setTransformForObject(
   //    t->objs[1],
   //    mat4Translate(-2,sin(angle),0) * mat4Scale(1));
   setTransformForObject(
      t->objs[1],
      mat4Translate(2*sin(angle),2,-2) * mat4Scale(1));
   setTransformForObject(
      t->objs[2],
      mat4Translate(0,-2,0) * mat4Scale(1));

   setLight(addLight(), LightDescription { /*pos*/{0, 0, 0}, /*color*/{1,1,1}, /*intensity*/5.0, /*bias*/1e-3, /*radius*/50} );
   setLight(addLight(), LightDescription { /*pos*/{0, 3.5, 0}, /*color*/{1,0,1}, /*intensity*/5.0, /*bias*/1e-3, /*radius*/10} );

   // Up down
   // setLight(addLight(), LightDescription { /*pos*/{0, 4*sinf(angle / 2), 0}, /*color*/{1,1,1}, 50.0, 0.1, 20} );

   // Left right
   // setLight(addLight(), LightDescription { /*pos*/{4*sinf(angle / 2), 1, 0}, /*color*/{1,1,1}, 50.0, 0.1, 10} );
   #else
   setLight(addLight(), LightDescription { /*pos*/{0, 5, 0}, /*color*/{1,1,1}, 50.0, {0,-5,0}, {1,0,0}} );
   setTransformForObject(
      t->sphere,
      mat4Scale(0.5));
   #endif

   endLightingEdit();

   gpuSetClearColor(0, 0.5, 0);
}

void pltestDeinit(u8* testMem)
{
   disposeWorld();
}