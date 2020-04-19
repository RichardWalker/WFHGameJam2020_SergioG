struct TestCode
{
   void (*init)(Platform*, u8*);
   void (*tick)(u8*, float);
   void (*deinit)(u8*);
};

struct
{
   Platform* plat;
   enum {
      Test_None,  // Black screen
      Test_Bunny,  // Load bunny
      Test_PointLight,
   } test;

   int testIdx;
   TestCode* sTests;
   u8 testMemory[1024];

   bool reloaded;

   u64 lastTickUs;
} *Tests;

u64 testGlobalSize() { return sizeof(*Tests); }
void testGlobalSet(u8* ptr)
{
   Tests = (decltype(Tests))ptr;
   Tests->reloaded = true;
}

void
defaultTestLightingAndCam()
{
   beginLightingEdit();

   setLight(addLight(), LightDescription { /*pos*/{0, 4, 0}, /*color*/{1,1,1}, /*intensity*/5.0, /*bias*/1e-3, /*far*/100.0} );

   endLightingEdit();

   // Set camera
   getWorld()->cam.eye = {0, 0, 4};
   getWorld()->cam.near = 0.1;
   getWorld()->cam.far = 10;
   getWorld()->cam.fov = DegreeToRadian(60.0);
}


#include "Tests/SingleText.cc"
#include "Tests/Bunny.cc"
#include "Tests/PointLight.cc"

#include "Tests/UnitTests.cc"

void empty(u8*) { }

void
runTestRegistration()
{
   SBResize(Tests->sTests, 0, Lifetime_App);

   #define RegisterTest(init, tick, deinit) \
      { \
         TestCode tc = { init, tick, deinit }; \
         SBPush(Tests->sTests, tc, Lifetime_App); \
      }

   // NOTE:

   RegisterTest(NULL, singleTextTick, NULL)
   RegisterTest(bunnyInit, bunnyTick, bunnyDeinit)
   RegisterTest(pltestInit, pltestTick, pltestDeinit)

   // Init first test.
   TestCode* test = Tests->sTests + Tests->testIdx;
   if (test->init) {
      test->init(Tests->plat, Tests->testMemory);
   }
}

void
testsInit(Platform* plat)
{
   Tests->plat = plat;

   // TODO: Init argument to run unit tests?
   runUnitTests();

   runTestRegistration();
}

void
testsTick()
{
   if (Tests->reloaded) {
      runTestRegistration();
      Tests->reloaded = false;
   }

   u64 now = Tests->plat->getMicroseconds();
   u64 frameUs = now - Tests->lastTickUs;

   float frameSec = (float)frameUs / 1000000.0;

   if (Tests->sTests[Tests->testIdx].tick) {
      Tests->sTests[Tests->testIdx].tick(Tests->testMemory, frameSec);
   }

   int delta = 0;
   if (keyJustPressed(Tests->plat, Key_Left)) {
      delta = -1;
   }
   if (keyJustPressed(Tests->plat, Key_Right)) {
      delta = 1;
   }

   if (delta != 0) {
      TestCode* toDeinit = Tests->sTests + Tests->testIdx;
      if (toDeinit->deinit) {
         toDeinit->deinit(Tests->testMemory);
      }
      disposeWorld();  // Might be redundant, depending on the test.

      Tests->testIdx += delta;
      if (Tests->testIdx == -1) {
         Tests->testIdx = SBCount(Tests->sTests) - 1;
      }
      if (Tests->testIdx == SBCount(Tests->sTests)) {
         Tests->testIdx = 0;
      }

      TestCode* test = Tests->sTests + Tests->testIdx;
      if (test->init) {
         test->init(Tests->plat, Tests->testMemory);
      }
   }

   Tests->lastTickUs = now;
}