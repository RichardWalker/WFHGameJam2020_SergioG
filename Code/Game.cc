#define AssetPath(plat, path) assetPathForFrame(plat, "../JamAssets/" ## path)

// TODO: This should be part of the API
char *
assetPathForFrame(Platform* p, char* relPath)
{
   char* res = AllocateArray(char, MaxPath, Lifetime_Frame);
   {
      char* out = res;
      for (char* i = relPath; i && *i != '\0'; ++i) {
         *out++ = *i;
      }
   }
   p->fnameAtExeAscii(res, MaxPath);
   return res;
}

#define NumFlames 8
#define MaxEnemies 24
#define NumTrees 40
#define KillToWin 20

bool gGameJamBlackScreen = false;

// Savestate
struct Gameplay
{
   vec3 dudePos;
   vec3 dudeVel;
   vec3 dudeDir;
   int dudeHealth;

   vec3 camEye;
   vec3 camOffset;

   int woodCount;
   int killCount;

   vec3 firePos;
   float fireLevel;
   int numHounds;

   int treeHealths[NumTrees];
   // TODO:
   int houndHealths[MaxEnemies];
   int houndGrudge[MaxEnemies];  // 0 fire, 1 player
   vec3 houndPos[MaxEnemies];
   vec3 houndDir[MaxEnemies];
};

enum RootFsm
{
   RootFsm_Menu,
   RootFsm_Play,
   RootFsm_Dead,
   RootFsm_Win,

   RootFsm_Count
};

struct Menu
{
   int selection;
};

struct CollisionBox
{
   vec3 bounds;
   vec3 pos;
};

enum DudeState
{
   Dude_Idle,
   Dude_Swinging,
};

struct Dude
{
   int state;
   float swingT;
   int swingTreeIdx;
   int swingHoundIdx;

   CollisionBox coll;
   CollisionBox axeColl;
   Mesh collisionMesh;
   Mesh axeColM;
   ObjectHandle debugCollision;
   ObjectHandle debugAxeC;

   ObjectHandle head;
   ObjectHandle hair;
   ObjectHandle torso;
   ObjectHandle lhand;
   ObjectHandle rhand;
   ObjectHandle axe1;
   ObjectHandle axe2;

   Mesh headMesh;
   Mesh hairMesh;

   Mesh torsoMesh;

   Mesh lhandMesh;

   Mesh axe1Mesh;
   Mesh axe2Mesh;
   Mesh rhandMesh;
};

enum HoundFsm
{
   Hound_Hunting,
   Hound_Charging,
   Hound_Attacking,
};

struct GameState
{
   Platform* plat;
   World* world;
   Font font;
   int rootFsm;  // RootFsm enum

   // Debug flags
   bool visualizeCollisionMeshes;
   bool autoRestart;

   u64 deathTickUs;

   vec2 lastMousePos;
   vec3 camTargetEye;
   vec3 camTargetRight;

   Gameplay gp;
   Menu menu;

   Dude dude;

   LoadedSound sndSwing;
   LoadedSound sndTreeHit;
   LoadedSound sndTreeFall;
   LoadedSound sndHoundDie;
   LoadedSound sndHoundDieAxe;
   LoadedSound sndPlayerHit;

   // Meshes.

   Mesh houndMesh;
   Mesh groundTileMesh;
   Mesh treeMesh;
   Mesh treeCollision;

   // Flames
   CollisionBox fireCol;
   ObjectHandle flames[NumFlames];

   // Trees
   CollisionBox treeCols[NumTrees];
   ObjectHandle treeCollisionHnds[NumTrees];
   ObjectHandle treeHnds[NumTrees];

   // Hounds
   ObjectHandle houndHnds[MaxEnemies];
   CollisionBox houndColl[MaxEnemies];
   ObjectHandle houndCollisionHnds[MaxEnemies];
   float houndCharge[MaxEnemies];
   float houndAttack[MaxEnemies];
   bool houndAttackHit[MaxEnemies];
   int houndFsm[MaxEnemies];  // HoundFsm

   ObjectHandle groundTileHnd;

   u64 lastTickUs;
} * Game;

// Game globals boilerplate
u64 gameGlobalSize() { return sizeof(*Game); }
void gameGlobalSet(u8* ptr) { Game = (decltype(Game))ptr; }

void
gameplayInit()
{
   Game->gp = {};

   Gameplay* gp = &Game->gp;

   gp->dudePos = vec3{1, 0 ,0};
   gp->dudeDir = vec3{ 0, 0, -1 };
   gp->dudeHealth = 100;
   gp->camEye = vec3{ 0,2,3 };
   gp->camOffset = vec3{1,0,0};

   for (int i = 0; i < NumTrees; ++i) {
      gp->treeHealths[i] = 5;
   }
   for (int i = 0; i < MaxEnemies; ++i) {
      Game->houndCharge[i]=0.0;
      Game->houndAttack[i]=0.0;
      Game->houndFsm[i] = 0;
      Game->houndAttackHit[i] = false;
   }

   gp->firePos = {-0.5, 0.2, 0.0};

   gp->fireLevel = 10.0;

   Game->camTargetEye = gp->camEye;
   Game->camTargetRight = {1,0,0};
}

// Between 0,1
float
random()
{
   return rand() / (float)RAND_MAX;
}

float
fireSafe()
{
   return Max(Game->gp.fireLevel / 2.5, 2);
}

float
fireFar()
{
   return Game->gp.fireLevel * 1.3;
}

float
fireBias()
{
   return 1e-2 + ((Game->gp.fireLevel) / 1000);
}

vec4
flameColor()
{
   return {0.9,0.8,0.5,1.0};
}

void
gameSave()
{
   char* path = assetPathForFrame(Game->plat, "../gamesave.bin");
   FILE* fd = fopen(path, "wb");
   if (fd) {
      // Try forever.
      while (fwrite(&Game->gp, sizeof(Game->gp), 1, fd) != 1) {}
      fclose(fd);
   }
}

void
gameLoad()
{
   char* path = assetPathForFrame(Game->plat, "../gamesave.bin");
   FILE* fd = fopen(path, "rb");
   if (fd) {
      fread(&Game->gp, sizeof(Game->gp), 1, fd);
      fclose(fd);
   }
   else {
      gameplayInit();
   }
}

void
setNonmetallic(Material* m, vec4 color, float roughness)
{
   MaterialConstantsCB* c = &m->constants;

   c->albedo = color;
   c->specularColor = c->albedo;
   c->roughness = roughness;
}

void
setMetallic(Material* m, vec4 color, float roughness)
{
   MaterialConstantsCB* c = &m->constants;

   c->albedo = {};
   c->specularColor = color;
   c->roughness = roughness;
}

void
setupGroundMaterial(Material* m)
{
   setNonmetallic(m, vec4 { 0.43, 0.42, 0.37, 1.0 }, 0.8);
}

void
setupTreeMaterial(Material* m)
{
   MaterialConstantsCB* c = &m->constants;

   c->albedo = vec4 { 0.18, 0.18, 0.16, 1.0 };
   c->specularColor = c->albedo;
   c->roughness = 0.5;
}

void
showMenu()
{
   immInit(&Game->font);

   Game->rootFsm = RootFsm_Menu;
}

void
menuTick()
{
   immSetCursor(gpu()->fbWidth/2 - 300, gpu()->fbHeight / 2 - 200);
   immText("Do not venture into the darkness", FontSize_Venti);

   enum menuEnum {
      Menu_New,
      Menu_Load,
      Menu_Exit,
   };
   char* menuItems[] = {
      "new game",
      "load latest checkpoint",
      "exit",
   };

   immList(menuItems, ArrayCount(menuItems), Game->menu.selection);

   if (keyJustPressed(Game->plat, Key_Down)) {
      Game->menu.selection++;
   }
   if (keyJustPressed(Game->plat, Key_Up)) {
      Game->menu.selection--;
   }

   if (keyJustPressed(Game->plat, Key_Enter)) {
      if (Game->menu.selection == Menu_New) {
         gameplayInit();
         Game->rootFsm = RootFsm_Play;
      }
      else if (Game->menu.selection == Menu_Load) {
         gameLoad();
         Game->rootFsm = RootFsm_Play;
      }
      else if (Game->menu.selection == Menu_Exit) {
         Game->plat->engineQuit();
      }
   }

   if (keyJustPressed(Game->plat, Key_Escape)) {
      Game->rootFsm = RootFsm_Play;
   }

   Game->menu.selection %= ArrayCount(menuItems);

   // Instructions
   immSetCursor(gpu()->fbWidth - 400, 200);
   immText("How to play:");
   immSetCursor(gpu()->fbWidth - 450, 250);
      immText("WASD + mouse to move");
      immText("Left click to swing axe");
      immText("Right click to interact");


   // Signature
   immSetCursor(gpu()->fbWidth - 500, gpu()->fbHeight- 150);
   immText("A game by Sergio Gonzalez", FontSize_Medium);


}

void
scaleToBounds(Mesh* m, vec3 bounds)
{
   for (int i = 0; i < m->numVerts; ++i) {
      vec4* v = m->sPositions + i;
      mat4 xform = mat4Identity();
      xform[0][0] *= bounds.x;
      xform[1][1] *= bounds.y;
      xform[2][2] *= bounds.z;

      *v = xform * (*v);
   }
}

void
playLoadedSound(LoadedSound* snd)
{
   playAudio(snd->samples, snd->hz, snd->channels, snd->numBytes);
}

void
winGame()
{
   Game->rootFsm = RootFsm_Win;
   Game->deathTickUs = Game->plat->getMicroseconds();
   gGameJamBlackScreen = true;
}

void
killPlayer()
{
   gGameJamBlackScreen = true;
   Game->deathTickUs = Game->plat->getMicroseconds();
   Game->rootFsm = RootFsm_Dead;
   if (Game->autoRestart) {
      gameplayInit();
      Game->rootFsm = RootFsm_Play;
   }
}

void
gameInit(Platform* plat)
{
   float groundSize = 50;

   Game->plat = plat;
   Game->world = getWorld();

   Game->sndPlayerHit = mp3Load(plat, AssetPath(plat, "PlayerHit.mp3"), Lifetime_World);
   Game->sndHoundDie = mp3Load(plat, AssetPath(plat, "EnemyDying.mp3"), Lifetime_World);
   Game->sndHoundDieAxe = mp3Load(plat, AssetPath(plat, "EnemyDyingAxe.mp3"), Lifetime_World);
   Game->sndSwing = mp3Load(plat, AssetPath(plat, "Swing.mp3"), Lifetime_World);
   Game->sndTreeHit = mp3Load(plat, AssetPath(plat, "TreeHit.mp3"), Lifetime_World);
   Game->sndTreeFall = mp3Load(plat, AssetPath(plat, "TreeFall.mp3"), Lifetime_World);

   float sizes[FontSize_Count] = {};

   sizes[FontSize_Tiny] = 18;
   sizes[FontSize_Small] = 20;
   sizes[FontSize_Medium] = 30;
   sizes[FontSize_Big] = 40;

   char* fontPath = AssetPath(plat, "DancingScript-VariableFont_wght.ttf");
   fontInit(plat, &Game->font, fontPath, sizes);

   Game->groundTileMesh = objLoad(plat, AssetPath(plat, "UnitGroundTile.obj"), Lifetime_World);
   Game->groundTileHnd = addMeshToWorld(Game->groundTileMesh, "Ground tile");
   setupGroundMaterial(materialForObject(Game->groundTileHnd));

   // Trees
   {
      // Only debugging the first, as I think it's working.
      Game->treeCollision = objLoad(plat, AssetPath(plat, "UnitCube.obj"), Lifetime_World);
      Game->treeCollisionHnds[0] = addMeshToWorld(Game->treeCollision, "Tree collision");
      setMetallic(materialForObject(Game->treeCollisionHnds[0]), vec4{0,1,0,1}, 0.3);

      Game->treeMesh = objLoad(plat, AssetPath(plat, "Tree.obj"), Lifetime_World);
      vec3 treeBounds = vec3{0.5, 4, 0.5};

      scaleToBounds(&Game->treeCollision, treeBounds);

      int treesPerSide = (int)sqrt((float)NumTrees);

      int x = 0;
      int y = 0;

      for (int i = 0; i < NumTrees; ++i) {
         x = i % treesPerSide;
         y = i / treesPerSide;

         float xoff = 5;
         vec3 treePos = {
            xoff + -(groundSize/2.0f) + x * ((float)(groundSize) / treesPerSide),
            0,
            -(groundSize/2.0f) + y * ((float)(groundSize) / treesPerSide),
         };
         Game->treeCols[i].pos = treePos;
         Game->treeCols[i].bounds = treeBounds;
         Game->treeHnds[i] = addMeshToWorld(Game->treeMesh, "Tree");
         // setTransformForObject(Game->treeCollisionHnds[i], mat4Translate(Game->treeCols[i].pos));
         setTransformForObject(Game->treeHnds[i], mat4Translate(Game->treeCols[i].pos));
         setupTreeMaterial(materialForObject(Game->treeHnds[i]));
      }
   }

   // Load dude.
   {

      Dude* d = &Game->dude;

      d->axe1Mesh = objLoad(plat, AssetPath(plat, "Dude_Axe1.obj"), Lifetime_World);
      d->axe2Mesh = objLoad(plat, AssetPath(plat, "Dude_Axe2.obj"), Lifetime_World);
      d->headMesh = objLoad(plat, AssetPath(plat, "Dude_Head.obj"), Lifetime_World);
      d->hairMesh = objLoad(plat, AssetPath(plat, "Dude_Hair.obj"), Lifetime_World);
      d->lhandMesh = objLoad(plat, AssetPath(plat, "Dude_Lhand.obj"), Lifetime_World);
      d->rhandMesh = objLoad(plat, AssetPath(plat, "Dude_Rhand.obj"), Lifetime_World);
      d->torsoMesh = objLoad(plat, AssetPath(plat, "Dude_Torso.obj"), Lifetime_World);

      d->axe1 = addMeshToWorld(d->axe1Mesh, "Dude mesh");
      d->axe2 = addMeshToWorld(d->axe2Mesh, "Dude mesh");
      d->head = addMeshToWorld(d->headMesh, "Dude mesh");
      d->hair = addMeshToWorld(d->hairMesh, "Dude mesh");
      d->lhand = addMeshToWorld(d->lhandMesh, "Dude mesh");
      d->rhand = addMeshToWorld(d->rhandMesh, "Dude mesh");
      d->torso = addMeshToWorld(d->torsoMesh, "Dude mesh");

      d->collisionMesh = objLoad(plat, AssetPath(plat, "UnitCube.obj"), Lifetime_World);
      d->axeColM = objLoad(plat, AssetPath(plat, "UnitCube.obj"), Lifetime_World);
      // Transform the verts to cover the dude.
      d->coll.bounds = vec3{0.5,2.0,0.5};
      d->axeColl.bounds = vec3{0.7, 2.0, 0.7};

      scaleToBounds(&d->collisionMesh, d->coll.bounds);
      scaleToBounds(&d->axeColM, d->axeColl.bounds);

      d->debugCollision = addMeshToWorld(d->collisionMesh);
      d->debugAxeC = addMeshToWorld(d->axeColM);

      setMetallic(materialForObject(d->debugCollision), vec4{1,0,1,1}, 0.3);
      setMetallic(materialForObject(d->debugAxeC), vec4{1,0,0,1}, 0.3);

      // Frodo's left butt cheek
      setNonmetallic(materialForObject(d->axe1), vec4 { 0.21, 0.11, 0.06 }, 0.9);
      setMetallic(materialForObject(d->axe2), vec4 { 0.77, 0.78, 0.78 }, 0.2);
      setNonmetallic(materialForObject(d->hair), vec4 { 0.06, 0.04, 0.03 }, 0.8);
      vec4 skinColor = vec4 { 0.45, 0.30, 0.18 };
      float skinRoughness = 0.8;

      setNonmetallic(materialForObject(d->lhand), skinColor, skinRoughness);
      setNonmetallic(materialForObject(d->rhand), skinColor, skinRoughness);
      setNonmetallic(materialForObject(d->head), skinColor, skinRoughness);

      setNonmetallic(materialForObject(d->torso), vec4{0.43, 0.35, 0.24}, 0.9);
   }

   // Only setting up the first for now. Assuming it won't break?
   Mesh cm = objLoad(plat, AssetPath(plat, "UnitCube.obj"), Lifetime_Frame);
   Game->houndCollisionHnds[0] = addMeshToWorld(cm, "Hound collision");

   // Enemies
   {
      Game->houndMesh = objLoad(plat, AssetPath(plat, "Hound.obj"), Lifetime_World);
      for (int i = 0; i < MaxEnemies; ++i) {
         Game->houndHnds[i] = addMeshToWorld(Game->houndMesh, "Hound");
         objectSetFlag(Game->houndHnds[i], WorldObject_Visible, false);

         Game->houndColl[i].bounds = vec3{0.8,0.5,0.8};
         scaleToBounds(&cm, Game->houndColl[i].bounds);

         setNonmetallic(materialForObject(Game->houndHnds[i]), vec4{0.39, 0.14, 0.07, 1.0}, 0.8);
      }
   }

   // Fire
   {
      Game->fireCol.bounds = vec3{0.5, 0.2, 0.5};

      Mesh flameMesh = objLoad(plat, AssetPath(plat, "Flame.obj"), Lifetime_Frame);
      for (int i = 0; i < NumFlames; ++i) {
         Game->flames[i] = addMeshToWorld(flameMesh, "Dude mesh");
         objectSetFlag(Game->flames[i], WorldObject_CastsShadows, false);
         setNonmetallic(materialForObject(Game->flames[i]), flameColor(), 1.0);
      }
   }

   // Ground setup.
   {
      setTransformForObject(Game->groundTileHnd, mat4Scale(groundSize));
   }

   gameplayInit();

   showMenu();
}

bool
treeExists(int idx)
{
   return (Game->gp.treeHealths[idx] > 0);
}

bool
houndExists(int idx)
{
   return Game->gp.houndHealths[idx] > 0;
}

bool
boxCollides(CollisionBox* a, CollisionBox* b)
{
   bool outside =
      a->pos.x + a->bounds.x < b->pos.x - b->bounds.x ||
      a->pos.x - a->bounds.x > b->pos.x + b->bounds.x ||

      a->pos.y + a->bounds.y < b->pos.y - b->bounds.y ||
      a->pos.y - a->bounds.y > b->pos.y + b->bounds.y ||

      a->pos.z + a->bounds.z < b->pos.z - b->bounds.z ||
      a->pos.z - a->bounds.z > b->pos.z + b->bounds.z
   ;
   if (!outside) {
      return true;
   }
   return false;
}


bool
collidesWithTrees(CollisionBox* c, vec3 p, int* treeIdx = NULL)
{
   bool r = false;

   for (int i = 0; i < NumTrees; ++i) {
      if (treeExists(i)) {
         CollisionBox* t = &Game->treeCols[i];
         bool outside =
            p.x + c->bounds.x < t->pos.x - t->bounds.x ||
            p.x - c->bounds.x > t->pos.x + t->bounds.x ||

            p.y + c->bounds.y < t->pos.y - t->bounds.y ||
            p.y - c->bounds.y > t->pos.y + t->bounds.y ||

            p.z + c->bounds.z < t->pos.z - t->bounds.z ||
            p.z - c->bounds.z > t->pos.z + t->bounds.z
         ;
         if (!outside) {
            r = true;
            if (treeIdx) { *treeIdx = i; }
            break;
         }
      }
   }

   return r;
}

bool
collidesWithHounds(CollisionBox* c, vec3 p, int* houndIdx = NULL)
{
   bool r = false;

   for (int i = 0; i < MaxEnemies; ++i) {
      if (houndExists(i) && Game->houndFsm[i] != Hound_Attacking) {
         CollisionBox* t = &Game->houndColl[i];
         bool outside =
            p.x + c->bounds.x < t->pos.x - t->bounds.x ||
            p.x - c->bounds.x > t->pos.x + t->bounds.x ||

            p.y + c->bounds.y < t->pos.y - t->bounds.y ||
            p.y - c->bounds.y > t->pos.y + t->bounds.y ||

            p.z + c->bounds.z < t->pos.z - t->bounds.z ||
            p.z - c->bounds.z > t->pos.z + t->bounds.z
         ;
         if (!outside) {
            r = true;
            if (houndIdx) { *houndIdx = i; }
            break;
         }
      }
   }

   return r;
}


bool
collidesWithObstacles(CollisionBox* c, vec3 p)
{
   CollisionBox testC = *c;
   testC.pos = p;
   bool collides  =
      collidesWithTrees(c,p) ||
      collidesWithHounds(c,p) ||
      boxCollides(&testC, &Game->fireCol);
   return collides;
}


void
worldPosTooltip(vec3 wp, char* msg)
{
   Camera* c = &getWorld()->cam;
   float w = gpu()->fbWidth;
   float h = gpu()->fbHeight;
   vec4 wp4;
   wp4.xyz = wp;
   wp4.w = 1;

   mat4 lookat = mat4Lookat(c->eye, c->lookat, c->up);

   vec4 sp = mat4Persp(c, w/h) * lookat * wp4;
   sp /= sp.w;

   sp.y *= -1;
   sp += vec4{1,1,0,0};
   sp /= 2;

   sp *= vec4{w,h,1,1};

   immSetCursor(sp.x, sp.y);
   immText(msg);
}

void
increaseFire()
{
   Game->gp.fireLevel += 3;

   // Spawn a hound
   if (Game->gp.numHounds < MaxEnemies) {
      Game->gp.numHounds++;
   }

   for (int i = 0; i < Game->gp.numHounds; ++i) {
      if (!houndExists(i)) {
         Game->gp.houndHealths[i] = 5;
         Game->gp.houndPos[i] = vec3{-fireFar() + i * 10,0,-fireFar()};
         Game->houndFsm[i] = Hound_Hunting;
      }
   }

}

void
gameTick()
{

   float deltaTimeSec = Min(1/30.0, float(Game->plat->getMicroseconds() - Game->lastTickUs) / 1000000.0);

   // === Debug flags
   Game->visualizeCollisionMeshes = false;
   Game->autoRestart = false;

   if (!getWorld()) {
      setWorld(Game->world);
   }

   // Visibility of collision meshes
   {
      ObjectHandle toSet[] = {
         Game->dude.debugCollision,
         Game->dude.debugAxeC,
         Game->treeCollisionHnds[0],
         Game->treeCollisionHnds[1],
         Game->houndCollisionHnds[0],
      };

      for (ObjectHandle& h : toSet) {
         objectSetFlag(h, WorldObject_Visible, Game->visualizeCollisionMeshes);
         objectSetFlag(h, WorldObject_CastsShadows, false);
      }
   }

   Gameplay* gp = &Game->gp;

   // Dude frame events.  ==
   bool dudeHitTree = false;
   bool dudeHitHound = false;

   #if BuildMode(Debug)
   if (!modeTick(Game->plat))
   #endif
   {
      if (Game->rootFsm == RootFsm_Menu) {
         menuTick();
      }
      // Menu captures all input.
      else if (Game->rootFsm == RootFsm_Dead) {

         if (keyJustPressed(Game->plat, Key_Escape) || (Game->plat->getMicroseconds() - Game->deathTickUs) >= 1000*1000) {
            showMenu();
         }

         immSetCursor(gpu()->fbWidth / 2, gpu()->fbHeight / 2);
         immText("You died", FontSize_Big);
      }
      else if (Game->rootFsm == RootFsm_Win) {
         immSetCursor(gpu()->fbWidth / 2, gpu()->fbHeight / 2);
         immText("You have won the game!", FontSize_Big);
         if (Game->plat->getMicroseconds() - Game->deathTickUs >= 2000*1000) {
            immText("Press ESC for menu. Alt+F4 to quit.");
         }

         if (keyJustPressed(Game->plat, Key_Escape) || (Game->plat->getMicroseconds() - Game->deathTickUs) >= 5*1000*1000) {
            showMenu();
         }

      }
      else if (Game->rootFsm == RootFsm_Play) {
         gGameJamBlackScreen = false;

         // Main game input + update ====

         if (keyJustPressed(Game->plat, Key_Escape)) {
            showMenu();
         }

         if (keyJustPressed(Game->plat, Key_i)) {
            winGame();
         }

         MouseInput* mouse = getMouseInput(Game->plat);
         if (mouse->numMouseMove) {
            Game->lastMousePos = vec2{
               (float)mouse->mouseMove[ mouse->numMouseMove - 1 ].x,
               (float)mouse->mouseMove[ mouse->numMouseMove - 1 ].y};
         }

         int deltaX = Game->lastMousePos.x - (gpu()->fbWidth / 2);

         // Dude controls
         {
            if (gp->dudeHealth <= 0) {
               killPlayer();
            }
            for (int i = 0; i < mouse->numClicks; ++i) {
               if (mouse->clicks[i].left == MouseDown && Game->dude.state == Dude_Idle) {
                  Game->dude.state = Dude_Swinging;
                  Game->dude.swingT = 0.0;
                  Game->dude.swingTreeIdx = NumTrees;
                  Game->dude.swingHoundIdx = MaxEnemies;
                  playLoadedSound(&Game->sndSwing);
               }
            }
            float speed = 1.5; // In meters per second
            float accelF = 30.0; // In meters per second squared.

            // Compute direction from mouse input.

            vec3 eye = getWorld()->cam.eye;

            vec4 dirp = {};
            dirp.xyz = gp->dudeDir;
            dirp.w = 1;
            if (deltaX) {
                int x = 0;
            }
            float rad = DegreeToRadian((float)deltaX) * 0.25;
            gp->dudeDir = (mat4Euler(0,0,rad) * dirp).xyz;

            vec3 dudeUp = { 0, 1, 0 };
            vec3 dudeZ = gp->dudeDir;
            vec3 dudeRight = normalizedOrZero(cross(dudeUp, dudeZ));

            vec3 accel = {};

            if (keyHeld(Game->plat, Key_w)) {
               accel += dudeZ;
            }
            if (keyHeld(Game->plat, Key_s)) {
               // In meters per second
               accel -= dudeZ;
            }
            if (keyHeld(Game->plat, Key_a)) {
               // In meters per second
               accel -= dudeRight;
            }
            if (keyHeld(Game->plat, Key_d)) {
               // In meters per second
               accel += dudeRight;
            }
            accel = normalizedOrZero(accel) * accelF;

            float drag = 0.1f;
            gp->dudeVel += accel * deltaTimeSec - gp->dudeVel * drag;

            float m = 5.0;
            vec3 directions[] = {
               vec3 { 0,0,0},

               vec3{ -m, 0, -m },
               vec3{ 0.0, 0, -m },
               vec3{ m, 0, -m },

               vec3{ -m, 0, 0 },
               vec3{ 0.0, 0, 0 },
               vec3{ m, 0, 0 },

               vec3{ -m, 0, m },
               vec3{ 0.0, 0, m },
               vec3{ m, 0, m },
            };
            for (int attempt = 0; attempt < ArrayCount(directions); ++attempt) {
               vec3 vel = gp->dudeVel + directions[attempt];

               vec3 nPos = gp->dudePos + vel * speed * deltaTimeSec;
               if (!collidesWithObstacles(&Game->dude.coll, nPos)) {
                  gp->dudePos = nPos;
                  gp->dudeVel = vel;
                  break;
               }
               else if (attempt == ArrayCount(directions) - 1) {
                  gp->dudeVel *= -1;
               }
            }

            // Do we hit trees.
            if (Game->dude.state == Dude_Swinging && Game->dude.swingT > 0.2) {
               if (Game->dude.swingTreeIdx == NumTrees &&
                  collidesWithTrees(&Game->dude.axeColl, Game->dude.axeColl.pos, &Game->dude.swingTreeIdx)) {
                  dudeHitTree = true;
                  playLoadedSound(&Game->sndTreeHit);
               }
               if (Game->dude.swingHoundIdx == MaxEnemies &&
                  collidesWithHounds(&Game->dude.axeColl, Game->dude.axeColl.pos, &Game->dude.swingHoundIdx)) {
                  dudeHitHound = true;
                  playLoadedSound(&Game->sndTreeHit);
               }
            }
         }

         // Tree tick
         {
            if (dudeHitTree) {
               if (--gp->treeHealths[Game->dude.swingTreeIdx] == 0) {
                  playLoadedSound(&Game->sndTreeFall);
                  gp->woodCount++;
                  gameSave();
               }
            }
            for (int i = 0; i < NumTrees; ++i) {
               objectSetFlag(Game->treeHnds[i], WorldObject_Visible, treeExists(i));
            }
         }

         // Fire tick
         {
            Game->fireCol.pos = gp->firePos;

            float fireDist = length(gp->firePos - gp->dudePos);
            if (fireDist < fireSafe() && gp->woodCount > 0 && (dot(gp->firePos - gp->dudePos, gp->dudeDir)) > 0.0) {
               worldPosTooltip(gp->firePos + vec3{0,2 + 0.02f*(gp->fireLevel - 10.0f),0}, "Right click: Throw");
               for (int i = 0; i < mouse->numClicks; ++i) {
                  if (mouse->clicks[i].right == MouseDown) {
                     gp->woodCount--;
                     increaseFire();
                  }
               }
            }

            float fireSpeed = 0.25;
            gp->fireLevel -= fireSpeed * deltaTimeSec;

            if (gp->fireLevel <= 0.0) {
               gp->fireLevel = 0;
               killPlayer();
            }
         }

         // Hound tick
         {
            for (int i = 0; i < MaxEnemies; ++i) {
               Game->houndColl[i].pos = gp->houndPos[i];

               objectSetFlag(Game->houndHnds[i], WorldObject_Visible, houndExists(i));
               if (houndExists(i)) {
                  // Fire kills them
                  if (boxCollides(&Game->houndColl[i], &Game->fireCol)) {
                     gp->houndHealths[i] = 0;
                     gp->houndGrudge[i] = 0; //fire
                  }
                  switch(Game->houndFsm[i]) {
                     case Hound_Hunting: {
                        // Dude* dude =
                        vec3 pos = gp->houndPos[i];
                        vec3 velocity = {};
                        vec3 toDude = gp->dudePos - pos;
                        if (length(toDude) < fireFar()*1.5) {
                           velocity = normalizedOrZero(toDude);
                        }
                        if (length(toDude) < 3) {
                           Game->houndFsm[i] = Hound_Charging;
                        }
                        gp->houndDir[i] = normalizedOrZero(toDude);
                        gp->houndPos[i] = pos + velocity * 5.0* deltaTimeSec;

                        Game->houndCharge[i] = 0;
                        Game->houndAttack[i] = 0;
                        Game->houndAttackHit[i] = false;
                     } break;

                     case Hound_Charging: {
                        Game->houndCharge[i] += 2.0 * deltaTimeSec;
                        if (Game->houndCharge[i] >= 1.0) {
                           Game->houndFsm[i] = Hound_Attacking;
                        }
                     } break;

                     case Hound_Attacking: {
                        Game->houndAttack[i] += 2.0 * deltaTimeSec;
                        gp->houndPos[i] += (gp->houndDir[i] * 0.5 ) * Game->houndAttack[i];
                        if (Game->houndAttack[i] >= 1.0) {
                           Game->houndFsm[i] = Hound_Hunting;
                        }

                        if (Game->houndAttackHit[i] == false &&
                           boxCollides(&Game->dude.coll, &Game->houndColl[i])) {
                           gp->dudeHealth -= 20;
                           gp->dudeVel += gp->houndDir[i] * 10;  // Push the player
                           Game->houndAttackHit[i] = true;
                           playLoadedSound(&Game->sndPlayerHit);
                        }

                     } break;

                  }

                  if (dudeHitHound && Game->dude.swingHoundIdx == i) {
                     gp->houndHealths[i]--;
                     gp->houndGrudge[i] = 1; //player
                  }

                  if (gp->houndHealths[i] <= 0) {
                     if (gp->houndGrudge[i] == 0) {
                        playLoadedSound(&Game->sndHoundDie);
                     }
                     else {
                        playLoadedSound(&Game->sndHoundDieAxe);
                     }
                     gp->killCount++;
                  }

                  if (gp->killCount >= KillToWin) {
                     winGame();
                  }
               }
            }
         }

      }  // Play tick

      vec3 cc = flameColor().xyz * Max(gp->fireLevel - 10.0, 0) / (100 - 10.0);
      gpuSetClearColor(cc.x, cc.y, cc.z);

      vec3 dudeUp = { 0, 1, 0 };
      vec3 dudeZ = normalizedOrZero(Game->gp.dudeDir);
      vec3 dudeRight = normalizedOrZero(cross(dudeUp, dudeZ));

      vec3 shake = {};
      if (dudeHitTree || dudeHitHound) {
         shake = vec3{ 1,1,1} * 0.1;
      }


      vec3 camLookat = gp->dudePos + dudeRight * 2.0;
      gp->camEye = gp->dudePos - gp->dudeDir * 5 + vec3{0,3,0};
      vec3 up = vec3{0,1,0};
      up = cross(up, camLookat - gp->camEye);
      up = normalizedOrZero(cross(camLookat - gp->camEye, up));

      getWorld()->cam.lookat = camLookat + shake;
      getWorld()->cam.eye = gp->camEye + shake;
      getWorld()->cam.up = up;
      getWorld()->cam.near = 0.1;
      getWorld()->cam.far = 100;
      getWorld()->cam.fov = DegreeToRadian(70.0);
   } // Non-menu input.

   // Updates even while in menu. ====

   // Dude animation.
   {
      // vec3 dudeUp = { 0, 1, 0 };

      // vec3 dudeZ = normalizedOrZero(gp->dudeDir);
      // vec3 dudeRight = normalizedOrZero(cross(dudeUp, dudeZ));
      // dudeUp = normalizedOrZero(cross(dudeZ, dudeRight));

      Dude* d = &Game->dude;

      float freq = 3;
      float mag = length(gp->dudeVel) / 20;
      static float walk = 0.0f;
      walk += mag;
      if (walk > 2*3.14) {
         walk -= 2*3.14;
      }
      static float armswing = 0;
      armswing += length(gp->dudeVel) / 20.0;
      if (armswing > 2*3.14) {
         armswing -= 2*3.14;
      }
      float stepHeight = Abs(cos(walk)) * 0.25;

      // xform.cols[3].xyz = gp->dudePos;
      // xform.cols[2].xyz = dudeZ;
      // xform.cols[1].xyz = dudeUp;
      // xform.cols[0].xyz = dudeRight;
      mat4 xform = mat4Orientation(gp->dudePos + vec3{0,stepHeight,0}, gp->dudeDir, vec3{0,1,0});

      mat4 swingX = mat4Identity();
      if (d->state == Dude_Swinging) {
         swingX = mat4Euler(d->swingT * Pi/4,0,d->swingT * Pi * 0.8);
         d->swingT += 25.0 * deltaTimeSec * (0.1 + d->swingT);
         if (d->swingT >= 1.0) {
            d->state = Dude_Idle;
            d->swingTreeIdx = NumTrees;
            d->swingHoundIdx = MaxEnemies;
         }
      }

      mat4 larmX = mat4Translate(0, 0, cos(armswing));
      mat4 rarmX = mat4Translate(0, 0, -cos(armswing));

      setTransformForObject(d->head, xform);
      setTransformForObject(d->hair, xform);
      setTransformForObject(d->torso, xform);
      setTransformForObject(d->lhand, xform*larmX);
      setTransformForObject(d->rhand, xform*swingX*rarmX);
      setTransformForObject(d->axe1, xform*swingX*rarmX);
      setTransformForObject(d->axe2, xform*swingX*rarmX);

      d->coll.pos = xform[3].xyz;
      d->axeColl.pos = xform[3].xyz + normalizedOrZero(gp->dudeDir) * 1.5;

      setTransformForObject(d->debugCollision, mat4Translate(d->coll.pos));
      setTransformForObject(d->debugAxeC, mat4Translate(d->axeColl.pos));
   }

   // Hound animation.
   {
      for (int houndIdx = 0; houndIdx < MaxEnemies; ++houndIdx) {
         vec3 pos = gp->houndPos[houndIdx];
         vec3 dir = gp->houndDir[houndIdx];

         mat4 xform = mat4Orientation(pos, dir, vec3{0,1,0});
         if (houndExists(houndIdx)) {
            mat4 charge = mat4Euler(0, -Game->houndCharge[houndIdx] * Pi/2, 0);
            setTransformForObject(Game->houndHnds[houndIdx], xform * charge);
         }
         setTransformForObject(Game->houndCollisionHnds[houndIdx], mat4Translate(pos));
      }
   }

   // Flame animation.
   {

      static vec3 offs[NumFlames] = {};
      static u64 lastUpdateUs;

      if (Game->plat->getMicroseconds() - lastUpdateUs > 100*1000) {
         lastUpdateUs = Game->plat->getMicroseconds();
         for (int i  = 0; i < NumFlames; ++i) {
            offs[i] = vec3{ random(), random(), random() } * 0.4;
         }
      }

      for (int i  = 0; i < NumFlames; ++i) {
         setTransformForObject(Game->flames[i], mat4Translate(gp->firePos + offs[i]));
      }
   }

   // Lighting
   {
      beginLightingEdit();

      float intensity = gp->fireLevel;
      float far = fireFar();

      setLight(addLight(), LightDescription { /*pos*/gp->firePos, /*color*/flameColor().rgb, /*intensity*/intensity, /*bias*/fireBias(), /*far*/far});

      endLightingEdit();
   }

   // HUD
   if (Game->rootFsm == RootFsm_Play) {
      char woodCount[128] = {};
      snprintf(woodCount, 128, "Wood count: %d", gp->woodCount);
      immSetCursor(10, gpu()->fbHeight - 10);
      immText(woodCount, FontSize_Medium);

      char health[128] = {};
      snprintf(health, 128, "Health: %d/100", gp->dudeHealth);
      worldPosTooltip(Game->gp.dudePos + vec3{0,2,0}, health);
      immSetCursor(10, gpu()->fbHeight - 10);
      immText(woodCount, FontSize_Medium);


      int remaining = KillToWin - gp->killCount;
      char rem[128] = {};
      snprintf(rem, 128, "Enemies remaining: %d", remaining);
      immSetCursor(gpu()->fbWidth - 400, 30);
      immText(rem, FontSize_Big);
   }

   Game->lastTickUs = Game->plat->getMicroseconds();
}