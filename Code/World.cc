
static World* gWorld;

struct ObjectIterator
{
   ObjectHandle h;
   WorldObjectFlag flags;

   ObjectHandle end;

   Lifetime life;
};


World*
getWorld()
{
	return gWorld;
}

AABB
invalidAABB()
{
   AABB bb = {};
   bb.min = { MaxFloat, MaxFloat, MaxFloat };
   bb.max = { MinFloat, MinFloat, MinFloat };
   return bb;
}

AABB
computeBoundingBox(const Mesh& m)
{
   AABB bb = invalidAABB();
   for (u64 vi = 0; vi < m.numVerts; ++vi) {
      for (int i = 0; i < 3; ++i) {
         bb.min[i] = Min(bb.min[i], m.sPositions[vi][i]);
         bb.max[i] = Max(bb.max[i], m.sPositions[vi][i]);
      }
   }
   return bb;
}

AABB
computeBoundingBox(const Blob& b)
{
   AABB bb = invalidAABB();
   for (int ei = 0; ei < b.numEdits; ++ei) {
      const Blob::Edit& e = b.edits[ei];
      switch (e.type) {
         case BlobEdit_Sphere: {
            for (int i = 0; i < 3; ++i) {
               bb.min[i] = Min(bb.min[i], (e.center - e.radius)[i]);
               bb.max[i] = Max(bb.max[i], (e.center + e.radius)[i]);
            }
         } break;
         default: {
            Assert(false);  // Not implemented.
         }
      }
   }
   return bb;
}

bool
rayTriangleIntersection(vec3 o, vec3 d, vec4* positions, u32* indices, size_t numIndices, float* outT)
{
   for (int i = 0; i < numIndices; i += 3) {
      vec4* v0 = positions + indices[i];
      vec4* v1 = positions + indices[i+1];
      vec4* v2 = positions + indices[i+2];

      vec3 e1 = v1->xyz-v0->xyz;
      vec3 e2 = v2->xyz-v0->xyz;
      vec3 q = cross(d, e2);
      float a = dot(e1, q);

      if (almostEquals(a, 0)) {
         continue;
      }

      float f = 1.0f / a;
      vec3 s = o - v0->xyz;
      float u = f * dot(s, q);

      if ( u < 0.f) {
         continue;
      }

      vec3 r = cross(s, e1);
      float v = f * dot(d, r);
      if (v < 0.f && u+v >= 1.0) {
         continue;
      }

      if (outT) {
        *outT = f*dot(e2, r);
      }

      return true;
   }
   return false;
}

ObjectHandle
addMeshToWorld(Mesh mesh, char* debugName)
{
   World* w = getWorld();
   u64 idx = w->numObjects++;
   WorldObjectFlag flags = (WorldObjectFlag)(WorldObject_Mesh | WorldObject_Visible | WorldObject_CastsShadows);
   w->objects[idx].flags = flags;
   w->objects[idx].mesh = mesh;
   w->renderHandles[idx].flags = flags;
   w->renderHandles[idx].mesh = uploadMeshToGPU(mesh);
   w->boundingBoxes[idx] = computeBoundingBox(mesh);

   ObjectHandle h = {idx};

#if BuildMode(Debug)
   w->debugNames[idx] = debugName;
#endif

   // Set default material.
   Material* mat = materialForObject(h);

   PSOFlags MSflag = (gKnobs.useMultisampling) ? PSOFlags_MSAA : PSOFlags_None;

   mat->psoFlags = MSflag;
   mat->constants.albedo = { 0.5, 0.5, 0.5, 1.0};
   mat->constants.specularColor = mat->constants.albedo;
   mat->constants.roughness = 0.6;
   mat->constants.useFlatColor = 1;

   return h;
}

Mesh*
worldObjectMesh(ObjectHandle h)
{
   WorldObject* wo = getWorld()->objects + h.idx;
   Assert(wo->flags & WorldObject_Mesh);
   return &wo->mesh;
}

void
setWorld(World* world)
{
   gWorld = world;
}

World*
makeAndSetWorld()
{
   World* world = AllocateElem(World, Lifetime_World);
   setWorld(world);
   addMeshToWorld(makeQuad(0.0, 0, Lifetime_Frame));  // Sentinel object
   world->currentBlobEdit = ObjectHandle{0};

   world->cam.fov = gKnobs.fov;
   world->cam.eye = {0, 0, -2.0f};
   world->cam.up = { 0, 1, 0 };
   world->cam.lookat = { 0, 0, 0 };

   return world;
}

ObjectHandle
newBlob()
{
   World* l = getWorld();
   u64 idx = l->numObjects++;

   BlobRenderHandle h = {};
   h.materialHandle = makeMaterial();
   h.constantResource = gpuCreateResource(sizeof(BlobCB), "Blob constants");

   Blob b = {};
   b.renderHandle = h;

   WorldObject o = {};
   o.flags = (WorldObjectFlag)(WorldObject_Blob | WorldObject_Visible);
   o.blob = b;

   WorldObjectRenderHandle rh = {};
   rh.flags = o.flags;
   rh.blob = b.renderHandle;

   l->objects[idx] = o;
   l->renderHandles[idx] = rh;

   return ObjectHandle{idx};
}

Blob*
beginBlobEdit(ObjectHandle h)
{
   World* l = getWorld();

   Assert(l->objects[h.idx].flags & WorldObject_Blob);
   Assert(l->currentBlobEdit.idx == 0);
   Assert(h.idx < l->numObjects);

   l->currentBlobEdit = h;
   Blob* b = &l->objects[h.idx].blob;
   return b;
}

void
emitError(char* message)
{
   // TODO: Log this text to game window.
   OutputDebugStringA(message);
}

bool
isValidObjectHandle(ObjectHandle h)
{
   return h.idx != 0 && h.idx < getWorld()->numObjects;
}

static mat4*
transformPointer(ObjectHandle h)
{
   mat4* out = {};
   if (!isValidObjectHandle(h)) {
      emitError("Invalid object handle\n");
   }
   else {
      WorldObjectRenderHandle* rh = &getWorld()->renderHandles[h.idx];

      if (rh->flags & WorldObject_Blob) {
         out = &gWorldRender->sObjectTransforms[rh->blob.transformIdx];
      }
      else if (rh->flags & WorldObject_Mesh) {
         out = &gWorldRender->sObjectTransforms[rh->mesh.transformIdx];
      }
   }
   return out;
}

mat4
transformForObject(ObjectHandle h)
{
   return *transformPointer(h);
}

bool
objectTestFlag(ObjectHandle h, WorldObjectFlag flag)
{
   bool test = getWorld()->objects[h.idx].flags & flag;
   return test;
}

void
objectSetFlag(ObjectHandle h, WorldObjectFlag flag, bool set)
{
   int val = getWorld()->objects[h.idx].flags;
   if (set) {
      val |= (int)flag;
   }
   else {
      val &= ~(int)(flag);
   }
   getWorld()->objects[h.idx].flags = (WorldObjectFlag)val;
}

void
setTransformForObject(ObjectHandle h, mat4 transform)
{
   mat4* p = transformPointer(h);
   if (p) {
      *p = transform;
   }
}

static bool
objectIterateFilter(ObjectIterator* i)
{
   bool pass = false;
   if (getWorld()) {
      pass = i->h.idx < getWorld()->numObjects && (getWorld()->objects[i->h.idx].flags & i->flags) == i->flags;
   }
   return pass;
}

u64
objectIterateCount(WorldObjectFlag flags)
{
   ObjectIterator* iter = objectIterateBegin(flags);
   u64 e = iter->end.idx;
   u64 b = iter->h.idx;
   return (e >= b && b < getWorld()->numObjects)  ? 1 + (e - b) : 0;
}

ObjectIterator*
objectIterateBegin(WorldObjectFlag flags)
{
   Lifetime life = lifetimeBegin();
   ObjectIterator* iter = AllocateElem(ObjectIterator, life);
   iter->flags = flags;
   iter->life = life;

   iter->h.idx = 1;  // 0 is the sentinel obj.
   iter->end.idx = getWorld()->numObjects;
   // Advance to first valid entry
   while (iter->h.idx < getWorld()->numObjects && !objectIterateFilter(iter)) {
      iter->h.idx++;
   }
   // Find last entry
   while (iter->end.idx > iter->h.idx) {
      iter->end.idx--;
      if ((iter->flags == (getWorld()->objects[iter->end.idx].flags & iter->flags))) {
         break;
      }
   }

   return iter;
}

bool
objectIterateHasNext(ObjectIterator* i)
{
   bool pass = i->h.idx <= i->end.idx && i->h.idx < getWorld()->numObjects;
   return pass;
}

ObjectHandle
objectIterateNext(ObjectIterator* i)
{
   Assert(getWorld() && i->h.idx < getWorld()->numObjects);
   ObjectHandle h = i->h;

   // Advance to next;
   do {
      i->h.idx++;
   } while(!objectIterateFilter(i) && i->h.idx <= i->end.idx);

   return h;
}

void
objectIterateEnd(ObjectIterator* i)
{
   // Deallocate
   lifetimeEnd(i->life);
}

WorldObjectRenderHandle*
renderHandleForObject(ObjectHandle oh)
{
   WorldObjectRenderHandle* rh = &getWorld()->renderHandles[oh.idx];
   return rh;
}

void
endBlobEdit()
{
   World* l = getWorld();
   ObjectHandle h = l->currentBlobEdit;
   Assert(h.idx != -1);
   Blob& b = getWorld()->objects[h.idx].blob;
   uploadBlobToGPU(b);
   l->boundingBoxes[h.idx] = computeBoundingBox(b);
   l->currentBlobEdit = ObjectHandle{0};
}

void
disposeWorld()
{
   if (getWorld()) {
      ObjectIterator* iter = objectIterateBegin(WorldObject_Mesh);
      while (objectIterateHasNext(iter)) {
         ObjectHandle h = objectIterateNext(iter);

         MeshRenderHandle* rh = &renderHandleForObject(h)->mesh;

         gpuMarkFreeRenderMesh(rh, gpu()->frameCount);
      }
      objectIterateEnd(iter);

      // Dispose of sentinel render mesh
      gpuMarkFreeRenderMesh(&renderHandleForObject({})->mesh, gpu()->frameCount);

      freePages(Lifetime_World);

      gWorld = NULL;
   }
}