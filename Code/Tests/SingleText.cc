void singleTextInit(u8*) { }

void singleTextTick(u8*, float)
{
   gpuSetClearColor(0.0, 0.0, 0.0);

   // modeTick(Tests->plat);
   int roughTextWidth = 400;

   immSetCursor(gpu()->fbWidth/2 - roughTextWidth / 2, 100);
   immText("Use the arrow keys to flip.", FontSize_Big);
   immText("Left/right to change tests.", FontSize_Small);

   immSetCursor(gpu()->fbWidth/2 - roughTextWidth / 2, gpu()->fbHeight - 20);
   immText("(This screen actually doubles as a text rendering / UI layout test.)", FontSize_Tiny);
}
