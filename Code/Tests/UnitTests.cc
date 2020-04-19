#include "Engine.h"

#include "stdio.h"

bool gTestSuccess = true;

#define IsTrue(expr) if (!(expr)) { logMsg("!!! Test failed: " #expr  "\n"); gTestSuccess = false; }
#define IsFalse(expr) IsTrue(!(expr))

void
testRayTriangleIntersection()
{
   {
      vec3 origin = { 0, 0, 0 };
      vec3 dir = { 0, 0, -1 };

      vec4 verts[] = {
         { 2, 2, -1, 1},
         { -2, 0, -1, 1},
         { 2, -2, -1, 1},
      };

      u32 indices[] = {
         0,1,2
      };

      IsTrue (rayTriangleIntersection(origin, dir, verts, indices, 3));
      float t = 0;
      IsTrue (rayTriangleIntersection(origin, Vec3(0,0,1), verts, indices, 3, &t));
      IsTrue (t == -1);

   }
}

void
testAlignOpts()
{
   // I just want to check if my AlignPow2 macro is doing anything or if the compiler is optimizing things.

   volatile int param = 127;


   volatile int foo = AlignPow2(param, 128);
   volatile int bar = Align(param, 128);

   // DebugBreak();

   // MSVC from VS 2019 on /O2 does emit better code with the first one.
}

void
runUnitTests()
{
   testRayTriangleIntersection();
   testAlignOpts();
}
