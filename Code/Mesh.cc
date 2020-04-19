

// Helpers


Mesh
makeQuad(f32 cx, f32 cy, f32 w, f32 h, f32 z, vec4 color, Lifetime life, WindingOrder winding)
{
   Mesh mesh = {};

   SBPush(mesh.sPositions, Vec4(cx, cy, z, 1.0f), life);
   SBPush(mesh.sPositions, Vec4(cx+w, cy, z, 1.0f), life);
   SBPush(mesh.sPositions, Vec4(cx+w, cy+h, z, 1.0f), life);
   SBPush(mesh.sPositions, Vec4(cx, cy+h, z, 1.0f), life);

   SBPush(mesh.sTexcoords, Vec2(0, 1), life);
   SBPush(mesh.sTexcoords, Vec2(1, 1), life);
   SBPush(mesh.sTexcoords, Vec2(1, 0), life);
   SBPush(mesh.sTexcoords, Vec2(0, 0), life);

   for (int i = 0; i < 4; ++i) {
      SBPush(mesh.sNormals, Vec3(0,0,-1), life);
      SBPush(mesh.sColors, color, life);
   }

   if (winding == Winding_CW) {
      u16 indexData[] = { 0,2,1, 3,2,0 };
      for (u16 i : indexData) {
         SBPush(mesh.sIndices, i, life);
      }
   }
   else {
      u16 indexData[] = { 0,1,2, 2,3,0};
      for (u16 i : indexData) {
         SBPush(mesh.sIndices, i, life);
      }
   }

   mesh.numVerts = SBCount(mesh.sPositions);
   mesh.numIndices = SBCount(mesh.sIndices);
   return mesh;
}

Mesh
makeQuad(float side, float z, Lifetime life, WindingOrder winding)
{
   Mesh q = makeQuad(-side, -side, 2*side, 2*side, z, vec4{}, life, winding);
   return q;
}