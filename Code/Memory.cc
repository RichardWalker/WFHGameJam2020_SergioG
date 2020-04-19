// ==================
// Memory management.
// ==================

struct AllocPage
{
   u8* start;
   u64 used;
   u64 size;
   AllocPage* next;
};
struct AllocHeader
{
   u64 size;
   Lifetime life;
};

static struct MemorySystem
{
   AllocPage* heapLists[(sz)Lifetime_Count];
   bool usedExplicitLifetimes[gKnobs.maxExplicitLifetimes];

   u64 lifetimeCount;
   Lifetime apiLifetime[gKnobs.apiLifetimeStackSize];
   u64 alignmentCount;
   u64 apiAlignment[gKnobs.apiLifetimeStackSize];

} *gMem;

void
memInit()
{
   gMem->lifetimeCount = 1;
   gMem->alignmentCount = 1;
}

u64
memoryGlobalsSize()
{
   u64 size = sizeof(*gMem);
   return size;
}

void
memoryGlobalsSet(u8* ptr)
{
   gMem = (MemorySystem*)ptr;
}

u64
availablePageBytes(const AllocPage& page)
{
   u64 r = page.size - page.used;
   return r;
}

AllocPage&
getPage(const u64 desiredBytes, const Lifetime life)
{
   AllocPage** page = &gMem->heapLists[life];

   // TODO: If this linear walk ever becomes a problem, we can come up with something
   for (; *page; *page = (*page)->next) {
      if (availablePageBytes(**page) > desiredBytes) {
         break;
      }
   }

   if (!(*page)) {
      // Didn't find page big enough. Create one.
      // TODO: Call OS function
      (*page) = (AllocPage*)calloc(1, sizeof(AllocPage));
      u64 pageSize = Max(desiredBytes, gKnobs.pageSize);
      (*page)->start = (u8*)calloc(1, pageSize);
      (*page)->size = pageSize;
   }

   return **page;
}

u8*
allocateBytes(u64 numBytes, const Lifetime life, u64 alignment)
{
   u64 desiredBytes = sizeof(AllocHeader) + numBytes;

   AllocPage& page = getPage(desiredBytes + alignment, life);

   u8* bytes = page.start + page.used;

   u8* alignedBytes = alignment ? (u8*)AlignPow2(((u64)(bytes + sizeof(AllocHeader))), alignment) : bytes + sizeof(AllocHeader);
   u64 alignmentBytes = (u64)(alignedBytes - (bytes + sizeof(AllocHeader)));

   Assert (!alignment || alignmentBytes < alignment);

   Assert(availablePageBytes(page) >= desiredBytes + alignmentBytes);

   page.used += desiredBytes + alignmentBytes;

   AllocHeader* h = (AllocHeader*)(bytes + alignmentBytes);
   h->size = numBytes;
   h->life = life;

   return bytes + sizeof(AllocHeader) + alignmentBytes;
}

u8*
reallocateBytesFor3rd(const u8* ptr, const sz newSize)
{
   AllocHeader empty = {};
   Assert(gMem->lifetimeCount);
   empty.life = gMem->apiLifetime[gMem->lifetimeCount - 1];
   AllocHeader* h = ptr ? (AllocHeader*)ptr - 1 : &empty;

   u8* bytes = allocateBytes(newSize, h->life, gMem->apiAlignment[gMem->alignmentCount - 1]);
   if (h->size > 0) {
      memcpy(bytes, ptr, h->size);
   }
   return bytes;
}



void pushApiLifetime(Lifetime life)
{
   gMem->apiLifetime[gMem->lifetimeCount++] = life;
}

void popApiLifetime()
{
   Assert(gMem->lifetimeCount > 1);
   --gMem->lifetimeCount;
}

void pushApiAlignment(u64 byteAlign)
{
   gMem->apiAlignment[gMem->alignmentCount++] = byteAlign;
}

void popApiAlignment()
{
   Assert(gMem->alignmentCount > 1);
   --gMem->alignmentCount;
}



void
freePages(const Lifetime life)
{
   AllocPage* page = gMem->heapLists[life];
   while (page) {
      memset(page->start, 0, page->used);
      page->used = 0;
      page = page->next;
   }
}

Lifetime
lifetimeBegin()
{
   Lifetime life = Lifetime_User;
   for (int i = 0; i < gKnobs.maxExplicitLifetimes; ++i) {
      if (!gMem->usedExplicitLifetimes[i]) {
         life = (Lifetime)(Lifetime_User + i);
         gMem->usedExplicitLifetimes[i] = true;
         break;
      }
      Assert (i != gKnobs.maxExplicitLifetimes - 1);
   }
   return life;
}

void
lifetimeEnd(Lifetime life)
{
   Assert(life >= Lifetime_User);
   Assert(life < Lifetime_Count);

   freePages(life);
   gMem->usedExplicitLifetimes[life - Lifetime_User] = false;
}