// TODO: Use assimp (https://github.com/assimp/assimp)


#define MaxWord 64

struct ObjParser
{
   u8* line;
   u64 i;
   //u64 bytes;

   u8* fileBytes;
   u64 previousLineBytes;  // line = fileBytes + previousLineBytes
   u64 totalBytes;
};

static bool
eof(ObjParser& p)
{
   return p.i + p.previousLineBytes == p.totalBytes - 1;
}

static bool
lineTerminator(ObjParser& p)
{
   return p.line == nullptr || p.line[p.i] == '\0' || p.line[p.i] == '\n' || eof(p) || p.line[p.i] == '\r';
}

static void
copyWord(ObjParser& p, u8* word) {
   memset(word, 0, MaxWord);
   int outi = 0;
   while (p.line[p.i] != ' ' && p.line[p.i] != '\0' && !lineTerminator(p)) {
      word[outi++] = p.line[p.i++];
      Assert (outi < MaxWord);
   }
}

static u32
readUint(ObjParser& p, u8* word) {
   u32 v = 0;
   memset(word, 0, MaxWord);

   int outi = 0;
   while (p.line[p.i] != ' ' && p.line[p.i] != '\0' && !lineTerminator(p) && p.line[p.i] - '0' >= 0 && p.line[p.i] - '0' <= 9) {
      word[outi++] = p.line[p.i++];
      Assert (outi < MaxWord);
   }

   sz wordLen = strlen((char*)word);

   for (int pos = 0; pos < wordLen; ++pos) {
      char val = word[pos] - '0';
      v *= 10;
      v += val;
   }
   return v;
}

static float
readFloat(ObjParser& p, u8* word) {
   float f = 0.0f;

   copyWord(p, word);
   sz wordLen = strlen((char*)word);

   float sign = 1;
   if (word[0] == '-') {
      sign = -1;
      word++;
      wordLen--;
   }

   i64 period = -1;
   for (sz i = 0; i < wordLen; ++i) {
      if (word[i] == '.') {
         period = i;
         break;
      }
   }

   float mag = 1.0f;
   for (int pos = period - 1; pos >= 0; --pos) {
      char val = word[pos];
      f += mag * (val - '0');
      mag *= 10;
   }
   mag = 1.0f / 10.0f;
   for (int pos = period + 1; pos < wordLen; pos++) {
      char val = word[pos];
      f += mag * (val - '0');
      mag /= 10;
   }

   return sign*f;
}

static void
skipWhitespace(ObjParser& p) {
   while (!eof(p) && p.line[p.i] == ' ') {
      p.i++;
   }
}

static bool
getNextLine(ObjParser& p, u8* origBytes)
{
   if (eof(p)) {
      return false;
   }
   u8* old = p.line;
   if (p.line) {
      // Advance to next line
      while (!lineTerminator(p)) {
         p.i++;
      }
      while (lineTerminator(p)) {
         if (eof(p)) {
            break;
         }
         p.i++;
      }
      p.line += p.i;
      p.previousLineBytes += p.i;
      p.i = 0;
   }
   else {
      p.line = origBytes;
   }
   return old != p.line;
}

Mesh
objLoad(Platform* plat, char* path, Lifetime life)
{
   pushApiLifetime(life);
   Mesh mesh = {};

   u8* data = nullptr;
   u64 numBytes = plat->fileContentsAscii(path, data, Lifetime_Frame);

   vec4* positions = nullptr;
   vec2* texcoords = nullptr;
   vec3* normals = nullptr;

   struct Triangle {
      u32 av = 0, at = 0, an = 0;
      u32 bv = 0, bt = 0, bn = 0;
      u32 cv = 0, ct = 0, cn = 0;
   };
   Triangle* tris = nullptr;

   ObjParser p = { nullptr, 0, data, 0, numBytes };
   while (getNextLine(p, data)) {
      if (p.line[0] == '#') {
         continue;
      }
      else {
         u8 word[MaxWord + 1] = {};
         copyWord(p, (u8*)(word));
         if (!strcmp((const char*)word, "v")) {
            vec4 v = {};
            skipWhitespace(p);
            v.x = readFloat(p, word);
            skipWhitespace(p);
            v.y = readFloat(p, word);
            skipWhitespace(p);
            v.z = readFloat(p, word);
            v.w = 1.0f;
            arrput(positions, v);
         }
         else if (!strcmp((const char*)word, "vn")) {
            vec3 v = {};
            skipWhitespace(p);
            v.x = readFloat(p, word);
            skipWhitespace(p);
            v.y = readFloat(p, word);
            skipWhitespace(p);
            v.z = readFloat(p, word);
            arrput(normals, v);
         }
         else if (!strcmp((const char*)word, "vt")) {
            vec2 v = {};
            skipWhitespace(p);
            v.u = readFloat(p, word);
            skipWhitespace(p);
            v.v = readFloat(p, word);
            arrput(texcoords, v);
         }
         else if (!strcmp((const char*)word, "f")) {
            skipWhitespace(p);
            Triangle v = {};
            v.av = readUint(p, word) - 1;
            ++p.i;
            v.at = readUint(p, word) - 1;
            ++p.i;
            v.an = readUint(p, word) - 1;
            skipWhitespace(p);
            v.bv = readUint(p, word) - 1;
            ++p.i;
            v.bt = readUint(p, word) - 1;
            ++p.i;
            v.bn = readUint(p, word) - 1;
            skipWhitespace(p);
            v.cv = readUint(p, word) - 1;
            ++p.i;
            v.ct = readUint(p, word) - 1;
            ++p.i;
            v.cn = readUint(p, word) - 1;

            arrput(tris, v);
         }
      }
   }


   pushApiLifetime(Lifetime_Frame);
   pushApiAlignment(16);  // u128 alignment...

      struct UniqueVert
      {
         vec4 position;
         vec2 texcoord;
         vec3 normal;
      };
      struct VertHashmap
      {
         meow_u128 key;
         UniqueVert value;
      } *hmVerts = {};  // Same key and value... Just to get a quick and dirty set impl.

      for (sz i = 0; i < arrlen(tris); ++i) {
         Triangle tri = tris[i];

         UniqueVert a = {};
         a.position = positions[tri.av];
         a.texcoord = texcoords[tri.at];
         a.normal = normals[tri.an];
         UniqueVert b = {};
         b.position = positions[tri.bv];
         b.texcoord = texcoords[tri.bt];
         b.normal = normals[tri.bn];
         UniqueVert c = {};
         c.position = positions[tri.cv];
         c.texcoord = texcoords[tri.ct];
         c.normal = normals[tri.cn];

         meow_u128 ah = MeowHash(MeowDefaultSeed, sizeof(UniqueVert), &a);
         meow_u128 bh = MeowHash(MeowDefaultSeed, sizeof(UniqueVert), &b);
         meow_u128 ch = MeowHash(MeowDefaultSeed, sizeof(UniqueVert), &c);

         hmput(hmVerts, ah, a);
         hmput(hmVerts, bh, b);
         hmput(hmVerts, ch, c);

         u32 ai = hmgeti(hmVerts, ah);
         u32 bi = hmgeti(hmVerts, bh);
         u32 ci = hmgeti(hmVerts, ch);

         // Output a triangle!
         arrput(mesh.sIndices, ai);
         arrput(mesh.sIndices, bi);
         arrput(mesh.sIndices, ci);
      }
   popApiLifetime(); // Frame
   popApiAlignment();

   // Output the verts
   for (sz i = 0; i < hmlen(hmVerts); ++i) {
      arrput(mesh.sPositions, hmVerts[i].value.position);
      arrput(mesh.sNormals, hmVerts[i].value.normal);
      arrput(mesh.sTexcoords, hmVerts[i].value.texcoord);
      arrput(mesh.sColors, Vec4(1,0,1,1));
   }

   mesh.numVerts = hmlen(hmVerts);
   mesh.numIndices = arrlen(mesh.sIndices);

   popApiLifetime();  // life
   return mesh;
}