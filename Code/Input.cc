#include "InputInternal.h"

MouseInput*
getMouseInput(Platform* plat)
{
   return &plat->getInput()->m;
}

bool
keyJustPressed(Platform* plat, Key k)
{
   return plat->getInput()->k.special[k] && plat->getInput()->k.specialTransition[k];
}

bool
keyJustReleased(Platform* plat, Key k)
{
   return !plat->getInput()->k.special[k] && plat->getInput()->k.specialTransition[k];
}

bool
keyHeld(Platform* plat, Key k)
{
   return plat->getInput()->k.special[k];
}

bool
getResizeInput(Platform* plat, int* w, int* h)
{
   ResizeInput* r = &plat->getInput()->r;
   if (r->withResize) {
      if (w) *w = r->width;
      if (h) *h = r->height;
   }
   return r->withResize;
}
