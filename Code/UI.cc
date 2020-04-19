// Immediate mode UI

struct TextCmd;


namespace Imm
{
   static vec4 BG = { 0.3, 0.3, 0.3, 1.0f };
   static vec4 FGcold = { 0.6, 0.6, 0.6, 1.0f };
   static vec4 FGhot = { 0.9, 0.6, 0.6, 1.0f };
   static vec4 FGactive = { 1.0, 0.0, 0.0, 1.0f };
   static int lineSpacing = 10;
};

struct UIRenderElem
{
   bool toRender;
   MeshRenderHandle mesh;
   ResourceHandle cstHnd;
   mat4 transform;
};

using UIId = u64;

struct UIElem
{
   UIId id;
   // In window pixel coordinates:
   f32 x;
   f32 y;
   f32 w;
   f32 h;
};

static struct ImmUI
{
   Font* t;

   vec2 cursor;  // Maybe rename?

   struct TextHM
   {
      meow_u128 key;
      UIRenderElem value;
   } *hmTexts;

   struct UIIdHM
   {
      void* key;
      UIElem value;
   } *hmElems;

   UIId hotUI;
   UIId activeUI;
   UIId newHotUI;

   u64 nextGUIId;

   u32 numRects;
   Mesh* sRects;
   UIRenderElem rects;

   // Layout
   bool sameLine;

   // Mouse handling:
   vec2 mouse;
   bool wasClicked;
   bool wasReleased;
} *gUI;

u64
immGlobalSize()
{
   return sizeof(*gUI);
}

void
immGlobalSet(u8* ptr)
{
   gUI = (ImmUI*)ptr;
}

void
immiPushRect(f32 cx, f32 cy, f32 w, f32 h, vec4 color)
{
   Mesh q = makeQuad(cx, cy, w, h, 0, color, Lifetime_Frame, Winding_CCW /* Will be reversed by UI shader*/);
   if (gUI->numRects >= arrlen(gUI->sRects)) {
      arrpush(gUI->sRects, q);
      gUI->numRects++;
   }
   else {
      gUI->sRects[gUI->numRects++] = q;
   }
}

void
immiWidgetBegin(f32 height)
{
   if (!gUI->sameLine) {
      gUI->cursor.y += height + Imm::lineSpacing;
   }
   gUI->sameLine = false;
}

void
immInit(Font* t)
{
   gUI->t = t;
   gUI->activeUI = static_cast<UIId>(-1);
   gUI->hotUI = static_cast<UIId>(-1);
   gUI->newHotUI = static_cast<UIId>(-1);
   gUI->sameLine = true;
}

void
immMouseMove(i32 x, i32 y)
{
   gUI->mouse = vec2{ (float)x, (float)y };
}

void
immClick()
{
   gUI->wasClicked = true;
}

void
immRelease()
{
   gUI->wasReleased = true;
}

void
immSetCursor(f32 x, f32 y)
{
   gUI->cursor = vec2{ x, y };
   gUI->sameLine = true;
}

void
immSameLine()
{
   gUI->sameLine = true;
}

void
immNewFrame()
{
   gUI->newHotUI = static_cast<UIId>(-1);
   immSetCursor(0, 0);
   immSameLine();
}

// Draw text at cursor, then advance cursor Y down to the next line.
void
immText(char* text, FontSize fsize)
{
   immiWidgetBegin(gUI->t->fontSizesPx[fsize]);
   if (strlen(text)) {
      // Build hash
      meow_u128 hash = MeowHash(MeowDefaultSeed, strlen(text), text);
      hash = _mm_xor_si128(hash, MeowHash(MeowDefaultSeed, sizeof(fsize), &fsize));

      i32 idx = hmgeti(gUI->hmTexts, hash);
      if (idx == -1) {
         UIRenderElem tr = {};
         buildLineOfText(*gUI->t, fsize, text, tr.mesh, tr.cstHnd);
         tr.toRender = true;
         tr.transform = mat4Translate(gUI->cursor.x, gUI->cursor.y, 0);
         hmput(gUI->hmTexts, hash, tr);
      } else {
         UIRenderElem* tr = &gUI->hmTexts[idx].value;
         tr->toRender = 1;
         tr->transform = mat4Translate(gUI->cursor.x, gUI->cursor.y, 0);

      }
   }
}

void
immTextInput(Platform* plat, char** sText, FontSize fsize)
{
   for (int k = Key_a; k <= Key_z; ++k) {
      if (keyJustPressed(plat, Key(k))) {
         arrpush(*sText, k);
      }
   }

   char* str = AllocateArray(char, arrlen(*sText) + 1, Lifetime_Frame);
   memcpy(str, *sText, arrlen(*sText));

   immText(str, fsize);
}

void
immList(char** texts, sz numTexts, int highlightedIdx, FontSize fsize)
{
   Assert(highlightedIdx == -1 || highlightedIdx < numTexts);
   for (sz i = 0; i < numTexts; ++i) {
      int off = 0;
      if (i == highlightedIdx) {
         immText("->");
         immSameLine();
         off = 20;
      }
      gUI->cursor.x += off;  // TODO: Get width and maybe change cursor with API?
      immText(texts[i], fsize);
      gUI->cursor.x -= off;
   }
}

bool
immSlider(float* ptr, float vmin = 0, float vmax = 1.0f, float width = 100, float height = 20) {
   immiWidgetBegin(height);

   i32 idx = hmgeti(gUI->hmElems, ptr);

   if (idx == -1) {
      idx = hmlen(gUI->hmElems);
      UIElem e;
      e.id = gUI->nextGUIId++;
      hmput(gUI->hmElems, ptr, e);
   }

   // We become hot if we're hovered and there's no-one active
   f32 x = gUI->cursor.x;
   f32 y = gUI->cursor.y - height;
   f32 w = width;
   f32 h = height;

   #define hot() (gUI->hotUI == idx)
   #define active() (gUI->activeUI == idx)

   if (gUI->activeUI == -1) {
      vec2 m = gUI->mouse;
      bool outside = m.x < x || m.x >= (x + w) || m.y < y || m.y >= (y + h);
      if (!outside) {
         gUI->newHotUI = idx;
      }
   }
   if (hot()) {
      if (gUI->wasClicked) {
         gUI->activeUI = idx;
      }
   }
   if (active()) {
      if (gUI->wasReleased) {
         gUI->activeUI = static_cast<UIId>(-1);
      }

      vec2 m = gUI->mouse;
	   float v = clamp((m.x - x) / (float)(w), 0, 1) * (vmax - vmin) + vmin;
      *ptr = v;
   }

   // Send draw commands.

   vec2 c = gUI->cursor;
   /**

   cx, cy-height      cx + width, cy - height
       ________________________
      |                       |
      |   ___                 |
      |__|  |_________________|
      |  |__|                 |
      |                       |
      |_______________________|  cx + width, cy
   cx, cy
   **/

   float t = (*ptr - vmin) / (vmax - vmin);
   // Maybe these should be tweakable?
   float sw = width / 5.0f;
   float sh = height * 0.8f;
   float sx = c.x + (t * width) + lerp(sw/2.0, -sw/2.0, t);
   float sy = c.y - height/2.0;
   immiPushRect(c.x, c.y - height, width, height, Imm::BG);

   vec4 color = active() ? Imm::FGactive : hot() ? Imm::FGhot : Imm::FGcold;
   immiPushRect(sx - sw/2, sy - sh/2, sw, sh, color);

   return active();
}

vec4
screenToWorld(Camera* cam, float x, float y)
{
   mat4 lookatMat = mat4Lookat(cam->eye, cam->lookat, cam->up);
   mat4 persp = mat4Persp(cam, (float)gpu()->fbWidth / gpu()->fbHeight);
   mat4 viewProjection = persp * lookatMat;

   mat4 toWorld = mat4Inverse(viewProjection);

   vec4 sp = { 2*(x / gpu()->fbWidth)-1, 2*(1.0f - (y / gpu()->fbHeight)) - 1, cam->near, 1 };
   vec4 wp = toWorld * sp;
   wp /= wp.w;

   return wp;
}

ObjectHandle
immObjectPick()
{
   ObjectHandle result = {};
   World* l = getWorld();
   if (gUI->wasClicked && l && gUI->activeUI == -1 && gUI->hotUI == -1) {
      float minT = 1e23;
      ObjectIterator* iter = objectIterateBegin((WorldObjectFlag(WorldObject_Visible)));
      while (objectIterateHasNext(iter)) {
         ObjectHandle oh = objectIterateNext(iter);
         Camera* cam = &l->cam;

         vec4 wp = screenToWorld(cam, gUI->mouse.x, gUI->mouse.y);

         // Cast ray
         vec3 o = cam->eye;
         vec3 d = normalized(wp.xyz - o);

         AABB& bb = l->boundingBoxes[oh.idx];
         WorldObjectRenderHandle& rh = l->renderHandles[oh.idx];

         // Convert to object space.
         mat4 mat = mat4Inverse(transformForObject(oh));
         o = (mat*toVec4(o, 1)).xyz;
         d = (mat*toVec4(d, 0)).xyz;

         vec3 minTs = (bb.min - o) / d;
         vec3 maxTs = (bb.max - o) / d;

         bool checkedObject = false;

         for (int di = 0; di < 3; ++di) {
            float t[2] = { minTs[di], maxTs[di] };
            vec3 p[2] = { o + t[0] * d, o + t[1] * d};

            int dj = (di + 1) % 3;
            int dk = (di + 2) % 3;
            for (int pi = 0; pi < 2; ++pi) {
               bool outside =
                  p[pi][dj] > bb.max[dj] ||
                  p[pi][dj] < bb.min[dj] ||
                  p[pi][dk] > bb.max[dk] ||
                  p[pi][dk] < bb.min[dk];

               if (!outside) {
                  // Pick against mesh...
                  if (rh.flags & WorldObject_Mesh) {
                     Mesh* mesh = worldObjectMesh(oh);
                     checkedObject = true;
                     float objT = 0;
                     if (rayTriangleIntersection(o, d, mesh->sPositions, mesh->sIndices, SBCount(mesh->sIndices), &objT)) {
                        if (objT >= 0 && objT < minT) {
                           minT = objT;
                           result = oh;
                           break;
                        }
                     }
                  }
                  else {
                     Assert(!"Add support for blobs in object picker..");
                  }
               }
            }
            if (checkedObject) {
               break;
            }
         }
      }
      objectIterateEnd(iter);
   }

   return result;
}

void
immRender()
{
   gpuBeginMarker("UI");

   // TODO: Oh shit
   gpuSetViewport(0,0, gpu()->fbWidth, gpu()->fbHeight);
   gpuSetRenderTargets(gpuBackbuffer(), nullptr);

   static const int maxKeysToDel = 8;
   int nDel = 0;
   meow_u128 toDelete[maxKeysToDel] = {};

   if (gUI->numRects) {
      // TODO: Only re-upload when meshes have changed.
      gUI->rects.mesh = uploadMeshesToGPU(gUI->sRects, gUI->numRects, /*withMaterial*/false, /*blas*/false);
      gpuMarkFreeRenderMesh(&gUI->rects.mesh, gpu()->frameCount);
      {
         // TODO: Frame lifetime for constant resources
         gUI->rects.cstHnd = gpuCreateResource(sizeof(TextConstants), "Widget constants");
         gpuMarkFreeResource(gUI->rects.cstHnd, gpu()->frameCount);
      }

      renderWidgets(*gUI->t, gUI->rects.mesh, gUI->rects.cstHnd);
   }

   // Render text
   for (sz i = 0; i < hmlen(gUI->hmTexts); ++i) {
      ImmUI::TextHM& entry = gUI->hmTexts[i];
      if (entry.value.toRender) {
         renderText(*gUI->t, entry.value.mesh, entry.value.cstHnd, entry.value.transform);
         entry.value.toRender = false;
      }
      else if (nDel < maxKeysToDel) {
         toDelete[nDel++] = gUI->hmTexts[i].key;
      }
   }

   for (int i = 0; i < nDel; ++i) {
      auto key = toDelete[i];
      gpuMarkFreeRenderMesh(&hmget(gUI->hmTexts, key).mesh, gpu()->frameCount);
      gpuMarkFreeResource(hmget(gUI->hmTexts, key).cstHnd, gpu()->frameCount);
      hmdel(gUI->hmTexts, key);
   }

   // Rendered all rects. Mark all as unused.
   gUI->numRects = 0;

   // Restore event state
   gUI->wasClicked = false;
   gUI->wasReleased = false;

   gUI->hotUI = gUI->newHotUI;

   gpuBarrierForResource(
      gpuRenderTargetResource(gpuBackbuffer()),
      ResourceState_RenderTarget,
      ResourceState_Present);

   gpuEndMarker(); // UI
}
