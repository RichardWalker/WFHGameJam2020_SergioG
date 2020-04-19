#pragma once

// Set the build mode for the engine.
#define BuildModeInternal InternalEngineDebug

// BuildMode flag
//    Available flags:
//       BuildMode(Debug)
//       BuildMode(Release)
//
//    Example use:
//       #if BuildMode(Debug)
//          Do debug stuff
//       #endif
#define BuildMode(Type) BuildModeInternal == InternalEngine##Type

// Internal build mode enum.
#define InternalEngineDebug     1
#define InternalEngineRelease   2

#define Kilobytes(x) (1024 * x)
#define Megabytes(x) (x * Kilobytes(1024))
#define Gigabytes(x) (x * Megabytes(1024))

#define ArrayCount(ptr) (sizeof(ptr) / sizeof(*ptr))
#define AlignPow2(v, p) (((v) + (p) - 1) & ~((p) - 1))
#define Align(val, align) ((((val) + (align) - 1) / (align)) * (align))
#define Pi 3.14159265359f
#define DegreeToRadian(deg) (2 * Pi * ((deg) / 360.0f))
#define Max(a, b) ((a) > (b) ? (a) : (b))
#define Min(a, b) ((a) < (b) ? (a) : (b))
#define Abs(x) ((x) < 0 ? -(x) : x)
#define ByteArray(obj) (u8*)(&obj), (sizeof(obj))
#define MaxPath 260
#define OffsetOf(s, m) ( (size_t)&((s*)0)->m )
#define MaxFloat FLT_MAX
#define MinFloat FLT_MIN

// Debug macros
#if BuildMode(Debug)

   #define Assert(expr) do {  if (!(expr)) { *(char**)0 = "Do Not Eat"; }  } while(0)

   #define BreakWhen(condition) do { if (condition) { __debugbreak(); } } while(0)

#else

   #define Assert(expr)
   // BreakWhen should only be used when debugging

#endif

#ifdef min
   #undef min
#endif
#ifdef max
   #undef max
#endif
#ifdef near
   #undef near
#endif
#ifdef far
   #undef far
#endif


#include <inttypes.h>
#include <math.h>
#include <new>  // In-place new...

typedef size_t sz;
typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t i16;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint64_t u64;
typedef int64_t i64;
typedef float f32;

#define Export extern "C" __declspec(dllexport)

// Warning suppression
#if defined(_MSC_VER)
#pragma warning(push)

#pragma warning(disable:4201)  // nonstandard nameless structs
#pragma warning(disable:4200)  // non-standard zero sized array
#endif

enum Mode
{
   Mode_Play,
   Mode_Fly,
   Mode_Finder,
   Mode_MaterialEditor,

   Mode_Count,
};

enum RunMode
{
   RunMode_Game,
   RunMode_Tests,
};

extern bool gGameJamBlackScreen;

// Engine knobs
static struct EngineKnobs
{
   // Dynamic
   int useMultisampling = true;
   bool printFramerate = true;
   float fov = DegreeToRadian(90);
   bool useRaytracedShadows = false;
   bool withRtx = false;
   int syncInterval = 1;

   // Per-instance
   #if BuildMode(Debug)
      static constexpr bool gpuDebug = false;
   #elif BuildMode(Release)
      static constexpr bool gpuDebug = false; // Do not change.
   #endif

   static constexpr RunMode defaultRunMode = RunMode_Tests;
   static constexpr char* windowTitle = "Do not venture into the darkness";
   static const int width = 1280;
   static const int height = 720;

   // GPU constants
   static const int uploadHeapSizeBytes = Megabytes(128);   // Figure out upload size later.
   static const int MSSampleCount = 4;
   static const int swapChainBufferCount = 2;

   static const int numSRVDescriptors = 512;
   static const int numUAVDescriptors = 512;
   static const int numCBVDescriptors = 2;  // CBV descriptors only used for ray tracing.

   static const int numRTVDescriptors = 512;
   static const int numDSVDescriptors = 512;
   static const int maxMaterials = 256;

   // CPU constants
   static const u64 pageSize = Kilobytes(64);
   static const u64 apiLifetimeStackSize = 64;
   static const u64 maxExplicitLifetimes = 64;

   // Assets
   static const u32 maxLights = 32;
   static const u32 fontOversampling = 4;  // 1 for no oversampling
   static const u32 maxEdits = 100;
   static const u32 shadowResolution = 1024;
   static const u32 maxObjects = 256;
} gKnobs;

// ================================
// Memory
// ================================

enum Lifetime
{
   Lifetime_App,  // Memory lives until the app closes.
   Lifetime_World,
   Lifetime_Frame,  // Memory gets cleared every frame

   Lifetime_User,  // Explicit lifetimes

   Lifetime_Count = Lifetime_User + gKnobs.maxExplicitLifetimes,
};

void memInit();

void pushApiLifetime(Lifetime life);
void popApiLifetime();
void pushApiAlignment(u64 byteAlign);
void popApiAlignment();


#define AllocateElem(type, heap) (type*)allocateBytes(sizeof(type), heap)
#define AllocateArray(type, count, heap)  (type*)allocateBytes(sizeof(type) * (count), heap)

u8* allocateBytes(u64 numBytes, const Lifetime life, u64 alignment = 0);
u8* reallocateBytesFor3rd(const u8* ptr, const sz newSize);

void freePages(const Lifetime life);

Lifetime lifetimeBegin();
void lifetimeEnd(Lifetime life);

// Globals
u64 memoryGlobalsSize();
void memoryGlobalsSet(u8* ptr);

// ================================
// Platform
// ================================

enum Key
{
   Key_Enter,
   Key_Escape,
   Key_Space,
   Key_Shift,
   Key_Ctrl,
   Key_Alt,
   Key_Tab,
   Key_Left,
   Key_Right,
   Key_Up,
   Key_Down,

   Key_a = 'a',
   Key_b, Key_c, Key_d, Key_e, Key_f, Key_g,
   Key_h, Key_i, Key_j, Key_k, Key_l, Key_m, Key_n,
   Key_o, Key_p, Key_q, Key_r, Key_s, Key_t, Key_u,
   Key_v, Key_w, Key_x, Key_y, Key_z,

   Key_Count,
};

typedef wchar_t PlatChar;  // TODO: Per-platform PlatChar
struct Input;

#define PlatformFnameAtExeProcDef(name) void name(PlatChar* fname, size_t len)
typedef PlatformFnameAtExeProcDef(PlatformFnameAtExeProc);

#define PlatformFileContentsAsciiProcDef(name) u64 name(char* fname, u8*& data, Lifetime life)
typedef PlatformFileContentsAsciiProcDef(PlatformFileContentsAsciiProc);

#define PlatformFnameAtExeAsciiProcDef(name) void name(char* fname, size_t len)
typedef PlatformFnameAtExeAsciiProcDef(PlatformFnameAtExeAsciiProc);

#define PlatformFileSizeAsciiProcDef(name) u64 name(char* fname)
typedef PlatformFileSizeAsciiProcDef(PlatformFileSizeAsciiProc);

#define PlatfromToPlatStrProcDef(name) void name(const char* str, PlatChar* out, const size_t out_sz)
typedef PlatfromToPlatStrProcDef(PlatfromToPlatStrProc);

#define PlatformGetPlatformErrorProcDef(name) char* name()
typedef PlatformGetPlatformErrorProcDef(PlatformGetPlatformErrorProc);

#define PlatformEngineQuitProcDef(name) void name()
typedef PlatformEngineQuitProcDef(PlatformEngineQuitProc);

#define PlatformWindowHandleProcDef(name) void* name()
typedef PlatformWindowHandleProcDef(PlatformWindowHandleProc);

#define PlatformGetInputProcDef(name) Input* name()
typedef PlatformGetInputProcDef(PlatformGetInputProc);

#define GetMicrosecondsProcDef(name) u64 name()
typedef GetMicrosecondsProcDef(GetMicrosecondsProc);

#define PlatformLogProcDef(name) void name(char* msg)
typedef PlatformLogProcDef(PlatformLogProc);

#define PlatformGetClientRectProcDef(name) void name(int* w, int* h)
typedef PlatformGetClientRectProcDef(PlatformGetClientRectProc);

struct Platform
{
   RunMode runMode;
   int windowRectL;  // TODO: GAME JAM HACK
   int windowRectR;  // TODO: GAME JAM HACK
   int windowRectT;  // TODO: GAME JAM HACK
   int windowRectB;  // TODO: GAME JAM HACK

   PlatformFnameAtExeAsciiProc* fnameAtExeAscii;
   PlatformFnameAtExeProc* fnameAtExe;
   PlatformFileContentsAsciiProc* fileContentsAscii;
   PlatformFileSizeAsciiProc* fileSizeAscii;
   PlatfromToPlatStrProc* toPlatStr;
   PlatformGetPlatformErrorProc* getPlatformError;
   PlatformEngineQuitProc* engineQuit;
   PlatformWindowHandleProc* getWindowHandle;
   PlatformGetClientRectProc* getClientRect;
   PlatformGetInputProc* getInput;
   GetMicrosecondsProc* getMicroseconds;
   PlatformLogProc* consoleLog;
};

// Memory for global systems
enum GlobalTable
{
   GlobalTable_Memory,
   GlobalTable_Logging,
   GlobalTable_Audio,
   GlobalTable_Render,
   GlobalTable_WorldRender,
   GlobalTable_UI,
   GlobalTable_Tests,
   GlobalTable_Editor,
   GlobalTable_Gameplay,

   GlobalTable_Count
};



// ================================
// Logging
// ================================

void logInit(Platform*);

void logMsg(char* msg, ...);


u64 logGlobalSize();
void logGlobalSet(u8*);
// ================================
// Input
// ================================

#define MaxFrameMouseEvents 128
enum MouseClick
{
   None = 0,

   MouseUp = -1,
   MouseDown = 1,
};

struct MouseInput
{
   struct {int x; int y;} mouseMove[MaxFrameMouseEvents];
   int numMouseMove;

   struct { MouseClick left; MouseClick right; } clicks[MaxFrameMouseEvents];
   int numClicks;
};

bool getResizeInput(Platform* plat, int* w, int* h);
MouseInput* getMouseInput(Platform* plat);
bool keyJustPressed(Platform* plat, Key k);
bool keyJustReleased(Platform* plat, Key k);
bool keyHeld(Platform* plat, Key k);

// ================================
// Engine dll exports.
// ================================
struct ShaderCode;

#define AppInitCallbackProcDef(name) void name(Platform* plat, bool fullscreen, int width, int height)
#define AppTickCallbackProcDef(name) void name(Platform* plat)
#define AppDisposeProcDef(name) void name(Platform* plat)
#define GetGlobalSizeTableProcDef(name) void name(u64* sizes)
#define PatchGlobalTableProcDef(name) void name(u8** pointers)
#define AppLoadShadersProcDef(name) void name(ShaderCode* shaderCode)

typedef AppInitCallbackProcDef(AppInitProc);
typedef AppTickCallbackProcDef(AppTickProc);
typedef AppDisposeProcDef(AppDisposeProc);
typedef AppLoadShadersProcDef(AppLoadShadersProc);
typedef GetGlobalSizeTableProcDef(GetGlobalSizeTableProc);
typedef PatchGlobalTableProcDef(PatchGlobalTableProc);

Export GetGlobalSizeTableProcDef(getGlobalSizeTable);
Export PatchGlobalTableProc(patchGlobalTable);
Export AppInitCallbackProcDef(appInit);
Export AppTickCallbackProcDef(appTick);
Export AppDisposeProcDef(appDispose);
Export AppLoadShadersProcDef(appLoadShaders);

struct AppCode
{
   AppInitProc* appInit;
   AppTickProc* appTick;
   AppDisposeProc* appDispose;
   AppLoadShadersProc* appLoadShaders;
};

// Globals, defined by game code.
u64 gameGlobalSize();
void gameGlobalSet(u8* ptr);

// Test harness
u64 testGlobalSize();
void testGlobalSet(u8* ptr);

// ================================
// Audio
// ================================

struct AudioStream;

void audioInit();
void audioFrameEnd();

// Simple api. Just call this and the samples will play.
void playAudio(i16* samples, int hz, int channels, int numBytes);

// More complex api.
AudioStream* audioStreamBegin(int hz, int channels);
void submitAudioToStream(AudioStream* stream, i16* samples, int numBytes, bool endStream);
void audioStreamEnd(AudioStream* stream);

// Globals
u64 audioGlobalSize();
void audioGlobalSet(u8* ptr);


// ================================
// Wrapper over STB stretchy buffers
// ================================

// NOTE: Lifetime is set for the *first* allocation. Follow-up allocations to the same buffer are done to the lifetime of the first allocation.
#define SBPush(arr, elem, lifeForFirstAlloc) \
   pushApiLifetime(lifeForFirstAlloc);       \
   arrpush(arr, (elem));                       \
   popApiLifetime();

#define SBResize(arr, size, lifeForFirstAlloc) \
   pushApiLifetime(lifeForFirstAlloc);       \
   arrsetlen(arr, size);                       \
   popApiLifetime();

#define SBCount(arr) \
   arrlen(arr)


// ================================
// Math
// ================================

struct Camera;

struct vec2
{
   union
   {
      struct { float x; float y; };
      struct { float u; float v; };
   };
};

struct vec3
{
   union
   {
      float data[3];
      struct { float x; float y; float z; };
      struct {float r; float g; float b; };
   };

   float& operator[](int i) {return data[i]; }
   float operator[](int i) const {return data[i]; }
};
struct vec4
{
   union
   {
      float data[4];
      struct { float x; float y; float z; float w; };
      struct { float r; float g; float b; float a; };
      struct { vec3 rgb; float a_; };
      struct { vec3 xyz; float w_; };
   };

   float& operator[](int i) { return data[i]; }

   float operator[](int i) const { return data[i]; }
};

struct mat4
{
   union
   {
      float data[16];
      vec4 cols[4];
   };

   vec4& operator[](int i) { return cols[i]; }

   vec4 operator[](int i) const { return cols[i]; }
};

struct AABB
{
   vec3 min;
   vec3 max;
};


vec4  row(const mat4& m, int j);
float lerp(float a, float b, float interp);
vec3  lerp(vec3 a, vec3 b, float interp);
mat4  mat4Scale(float scale);
mat4  mat4Euler(const float roll, const float pitch, const float yaw);
float dot(const vec2& a, const vec2& b);
float dot(const vec3& a, const vec3& b);
float dot(const vec4& a, const vec4& b);
float length(vec2 v);
float length(vec3 v);
float length(vec4 v);
vec4  toVec4(const vec3& v, float w);
mat4  mat4Identity();
bool  almostEquals(const float a, const float b);
vec3  cross(const vec3& a, const vec3& b);
float norm(const vec2& v);
float norm(const vec3& v);
vec2  normalized(const vec2& v);
vec3  normalized(const vec3& v);
vec2  normalizedOrZero(const vec2& v);
vec3  normalizedOrZero(const vec3& v);
vec4  saturate(const vec4& v);
mat4  mat4Transpose(const mat4& m);
mat4  mat4Lookat(const vec3& eye, const vec3& lookat, const vec3& up);
mat4  mat4Translate(f32 x, f32 y, f32 z);
mat4  mat4Translate(vec3 p);
mat4  mat4Inverse(const mat4& m);
mat4  mat4Persp(const Camera* c, float aspect);
mat4  mat4Orientation(vec3 pos, vec3 dir, vec3 up);
float signedArea(vec2 a, vec2 b, vec2 c);
float sign(float x);

// ==== vec2 operators
#define BinaryScalarOp(op) \
   vec2 operator op (const vec2& v, float s) { \
      vec2 r = {v.x op s, v.y op s  };\
      return r; \
   }\
   vec2 operator op (float s, const vec2& v) { \
      vec2 r = {  v.x op s, v.y op s }; \
      return r; \
   }


#define BinaryVecOp(op) \
   vec2 operator op (const vec2& a, const vec2& b) { \
      vec2 r = {  a.x op b.x, a.y op b.y, }; \
      return r; \
   }

#define BinaryVecAssignOp(op) \
   vec2& operator op (vec2& a, const vec2& b) { \
      a.x op b.x; \
      a.y op b.y; \
      return a; \
   }

#define BinaryScalarAssignOp(op) \
   vec2& operator op (vec2& a, float f) { \
      a.x op f; \
      a.y op f; \
      return a; \
   }

BinaryScalarOp(+)
BinaryScalarOp(-)
BinaryScalarOp(*)
BinaryScalarOp(/)

BinaryVecOp(+)
BinaryVecOp(-)
BinaryVecOp(*)
BinaryVecOp(/)

BinaryVecAssignOp(+=)
BinaryVecAssignOp(-=)
BinaryVecAssignOp(*=)
BinaryVecAssignOp(/=)

BinaryScalarAssignOp(+=)
BinaryScalarAssignOp(-=)
BinaryScalarAssignOp(*=)
BinaryScalarAssignOp(/=)

vec2 operator - (const vec2& v) { return vec2{ -v.x, -v.y }; }

#undef BinaryVecOp
#undef BinaryScalarOp
#undef BinaryVecAssignOp
#undef BinaryScalarAssignOp

// ==== vec3 operators

#define BinaryScalarOp(op) \
   vec3 operator op (const vec3& v, float s) { \
      vec3 r = {v.x op s, v.y op s, v.z op s }; \
      return r; \
   }\
   vec3 operator op (float s, const vec3& v) { \
      vec3 r = {v.x op s, v.y op s, v.z op s }; \
      return r; \
   }


#define BinaryVecOp(op) \
   vec3 operator op (const vec3& a, const vec3& b) { \
      vec3 r = { a.x op b.x, a.y op b.y, a.z op b.z }; \
      return r; \
   }

#define BinaryVecAssignOp(op) \
   vec3& operator op (vec3& a, const vec3& b) { \
      a.x op b.x; \
      a.y op b.y; \
      a.z op b.z; \
      return a; \
   }

#define BinaryScalarAssignOp(op) \
   vec3& operator op (vec3& a, float f) { \
      a.x op f; \
      a.y op f; \
      a.z op f; \
      return a; \
   }

BinaryScalarOp(+)
BinaryScalarOp(-)
BinaryScalarOp(*)
BinaryScalarOp(/)

BinaryVecOp(+)
BinaryVecOp(-)
BinaryVecOp(*)
BinaryVecOp(/)

BinaryVecAssignOp(+=)
BinaryVecAssignOp(-=)
BinaryVecAssignOp(*=)
BinaryVecAssignOp(/=)

BinaryScalarAssignOp(+=)
BinaryScalarAssignOp(-=)
BinaryScalarAssignOp(*=)
BinaryScalarAssignOp(/=)

vec3 operator - (const vec3& v)
{
   return vec3{ -v.x, -v.y, -v.z };
}

#undef BinaryVecOp
#undef BinaryScalarOp
#undef BinaryVecAssignOp
#undef BinaryScalarAssignOp


// ==== vec4 operators

#define BinaryScalarOp(op) \
   vec4 operator op (const vec4& v, float s) { \
      vec4 r = {v.x op s, v.y op s, v.z op s, v.w op s }; \
      return r; \
   }\
   vec4 operator op (float s, const vec4& v) { \
      vec4 r = {v.x op s, v.y op s, v.z op s, v.w op s }; \
      return r; \
   }

#define BinaryVecOp(op) \
   vec4 operator op (const vec4& a, const vec4& b) { \
      vec4 r = {a.x op b.x, a.y op b.y, a.z op b.z, a.w op b.w }; \
      return r; \
   }

#define BinaryVecAssignOp(op) \
   vec4& operator op (vec4& a, const vec4& b) { \
      a.x op b.x; \
      a.y op b.y; \
      a.z op b.z; \
      a.w op b.w; \
      return a; \
   }

#define BinaryScalarAssignOp(op) \
   vec4& operator op (vec4& a, float f) { \
      a.x op f; \
      a.y op f; \
      a.z op f; \
      a.w op f; \
      return a; \
   }

BinaryScalarOp(+)
BinaryScalarOp(-)
BinaryScalarOp(*)
BinaryScalarOp(/)

BinaryVecOp(+)
BinaryVecOp(-)
BinaryVecOp(*)
BinaryVecOp(/)

BinaryVecAssignOp(+=)
BinaryVecAssignOp(-=)
BinaryVecAssignOp(*=)
BinaryVecAssignOp(/=)

BinaryScalarAssignOp(+=)
BinaryScalarAssignOp(-=)
BinaryScalarAssignOp(*=)
BinaryScalarAssignOp(/=)

#undef BinaryVecOp
#undef BinaryScalarOp
#undef BinaryVecAssignOp
#undef BinaryScalarAssignOp

// mat4 operators
vec4 operator*(const mat4& m, const vec4& v);
mat4 operator*(const mat4& a, const mat4& b);

vec4 Vec4(f32 x, f32 y, f32 z, f32 w);
vec3 Vec3(f32 x, f32 y, f32 z);
vec2 Vec2(f32 x, f32 y);

// ================================
// Renderer
// ================================

struct ObjectHandle { u64 idx; };
struct MaterialHandle { int idx; };

struct PipelineStateHandle { u64 idx; int type; };
enum { PipelineStateHandle_ComputeGraphics, PipelineStateHandle_Raytracing };

struct ResourceHandle { u64 idx; };
struct WorldObjectRenderHandle;
struct LightHandle { int idx; };
struct BLASHandle { u64 idx; };
struct RenderTarget;

enum ResourceState
{
   ResourceState_GenericRead,
   ResourceState_CopyDest,
   ResourceState_VertexBuffer,
   ResourceState_IndexBuffer,
   ResourceState_PixelShaderResource,
   ResourceState_RenderTarget,
   ResourceState_DepthWrite,
   ResourceState_NonPixelShader,
   ResourceState_UnorderedAccess,
   ResourceState_AccelerationStructure,
   ResourceState_ResolveSource,
   ResourceState_ResolveDest,
   ResourceState_Present,

   ResourceState_Count,
};

enum GPUHeapType
{
   GPUHeapType_Default,
   GPUHeapType_Upload,

   GPUHeapType_Count,
};

enum WindingOrder
{
   Winding_CW,
   Winding_CCW,
};

enum PSOFlags
{
   PSOFlags_None = 0,

   PSOFlags_NoDepth = (1<<0),
   PSOFlags_AlphaBlend = (1<<1),
   PSOFlags_MSAA = (1<<2),
   PSOFlags_NoCull = (1<<3),
   PSOFlags_R8FloatTarget = (1<<4),
};

struct LightDescription
{
   vec3 position;
   vec3 color;
   float intensity;
   float bias;
   float far;
};

struct Camera
{
   float near;
   float far;

   float fov;

   vec3 lookat;
   vec3 eye;
   vec3 up;
};

// How vertices are packed in the GPU
struct MeshRenderVertex
{
   vec4 position;
   vec2 texcoord;
   vec3 normal;
   vec4 color;
};

__declspec(align(16))
struct MaterialConstantsCB
{
   // TODO: per-view constant buffer
      vec3 camPos;
      float pad0;

      vec3 camDir;
      float near;

      float far;
      float pad1[3];

      mat4 viewProjection;

      mat4 objectTransform;

      mat4 toWorld;

      // float viewWidth;
      // float viewHeight;
      // float pad[2];

      int raytracedShadowIdx;
      int useRaytracedShadows;

   int useFlatColor;
   int diffuseTexIdx;

   vec4 albedo;

   vec4 specularColor;

   float roughness;
   float pad3[3];
};

struct Material
{
   MaterialConstantsCB constants;
   PSOFlags psoFlags;
   ResourceHandle gpuResource;
};

enum class TextureFormat
{
   R8G8B8A8_UNORM,
   R32_UINT,
   D32_FLOAT,
   R32_FLOAT,
   R8_UNORM,
};

// TODO: Not sure if it would be better to have a single texture type.
struct Texture2D
{
   ResourceHandle resourceHandle;
   u64 srvBindIndex;
   u64 uavBindIndex;
};

struct Texture3D
{
   ResourceHandle resourceHandle;

   u16 mipCount;

   // TODO: How does this map to vulkan?
   u64 bindIndexSRV;
   u64 bindIndexUAV[16];
};

struct TextureCube
{
   ResourceHandle resourceHandle;
   TextureFormat format;
   u64 srvCubeBindIndex;  // Texture cube view
   u64 srvFaceBindIndex[6];  // 2D array
};

struct RenderMesh
{
   ResourceHandle vertexBuffer;
   ResourceHandle indexBuffer;
   sz vertexBytes;
   sz indexBytes;
   u64 numIndices;
};

enum BlobEditType
{
   BlobEdit_Sphere
};

struct RendererI
{
   i32 fbWidth;
   i32 fbHeight;
   u64 frameCount;
   u64 frameIdxModulo;
};

struct TLAS;
struct BLAS;

// ================================
// Core render API
// ================================


RendererI*        gpu();
void              gpuInit(Platform* plat, int w, int h);
void              gpuResize(int w, int h);
void              gpuFlush();
void              gpuFinishInit();
void              gpuDispose();
void              gpuGoFullscreen();


void              gpuPrepareForMainLoop();

// Pipeline states
PipelineStateHandle gpuGraphicsPipelineState(u8* vertexByteCode, u64 szVertexByteCode, u8* pixelByteCode, u64 szPixelByteCode, const PSOFlags psoFlags);
PipelineStateHandle gpuComputePipelineState(u8* code, u64 sz);

// Resource management.

enum CreateResourceFlags
{
   CreateResourceFlags_None = 0,
   CreateResourceFlags_AllowUnordered = (1<<0),
};
ResourceHandle    gpuCreateResource(size_t numBytes, char* debugName = nullptr, GPUHeapType heapType = GPUHeapType_Upload, ResourceState state = ResourceState_GenericRead, u32 alignment = 0, CreateResourceFlags flags = CreateResourceFlags_None);
void              gpuSetResourceData(ResourceHandle resHnd, const void* data, size_t numBytes);
void              gpuSetResourceDataAtOffset(ResourceHandle resHnd, const void* data, size_t numBytes, const sz offset);
void              gpuUploadBufferAtOffset(ResourceHandle destResource, const u8* data, const sz size, const sz offset);
void              gpuUploadBuffer(ResourceHandle destResource, const u8* data, const sz size);
void              gpuBarrierForResource(ResourceHandle resource, ResourceState before, ResourceState after);
void              gpuUAVBarrier(ResourceHandle resource);
void              gpuMarkFreeResource(ResourceHandle& res, u64 atFrame);

enum CubeFace
{
   CubeFace_PosX,
   CubeFace_NegX,
   CubeFace_PosY,
   CubeFace_NegY,
   CubeFace_PosZ,
   CubeFace_NegZ,
};

Texture2D         gpuCreateTexture2D(int width, int height, bool withUAV, TextureFormat format = TextureFormat::R8G8B8A8_UNORM, ResourceState state = ResourceState_CopyDest);
Texture2D         gpuUploadTexture2D(const int width, const int height, const int bytesPerPixel, const u8* data);
Texture3D         gpuCreateTexture3D(const int width, const int height, const int depth);
TextureCube       gpuCreateTextureCube(int width, int height, TextureFormat formatDesc, ResourceState stateDesc);

// Raytracing

struct RaytracingHitgroup
{
   wchar_t* name;

   wchar_t* closestHit;
   wchar_t* anyHit;
   wchar_t* intersection;
};


struct RaytracingPSODesc
{
   // Compiled shader
   u8* byteCode;
   u64 szByteCode;

   wchar_t** sMissIds;
   RaytracingHitgroup* sHitGroups;
};

// Acceleration structure. BLAS live in object instances, TLAS are per-frame.
BLASHandle        gpuMakeBLAS(ResourceHandle vertexBuffer, u64 numVerts, u64 vertexStride, ResourceHandle indexBuffer, u64 numIndices);
TLAS*             gpuCreateTLAS(u64 maxNumInstances);
void              gpuAppendToTLAS(TLAS* bvh, BLASHandle h, mat4 transform);
void              gpuBuildTLAS(TLAS* bvh);

void              gpuMarkFreeBLAS(BLASHandle h, u64 atFrame);
void              gpuSetRaytracingConstantSlot(u64 slot, ResourceHandle resource, u64 numBytes);  // TODO: ResourceHandle should validate resource sizes.

PipelineStateHandle gpuCreateRaytracingPipeline(RaytracingPSODesc desc);

// Get the current backbuffer targets.
RenderTarget*     gpuBackbuffer();
RenderTarget*     gpuDefaultDepthTarget();

enum RTFlags
{
   RTFlags_None,

   RTFlags_Multisampled = (1<<0),
   RTFlags_WithSRV = (1<<1),
   RTFlags_WithUAV = (1<<2),
};

vec3              gpuSetClearColor(float r, float b, float g);  // Returns the existing clear value.
RenderTarget*     gpuCreateColorTarget(int width, int height, ResourceState state, RTFlags flags = RTFlags_None);
RenderTarget*     gpuCreateDepthTarget(int width, int height, RTFlags flags = RTFlags_None);
RenderTarget*     gpuCreateDepthTargetForCubeFace(TextureCube* cube, int width, int height, CubeFace face);
RenderTarget*     gpuCreateColorTargetForCubeFace(TextureCube* cube, int width, int height, CubeFace face);
void              gpuSetRenderTargets(RenderTarget* rtColor, RenderTarget* rtDepth);
void              gpuClearRenderTargets(RenderTarget* color, RenderTarget* depth);
void              gpuMarkFreeColorTarget(RenderTarget* t, u64 atFrame);
void              gpuMarkFreeDepthTarget(RenderTarget* t, u64 atFrame);
void              gpuSetViewport(int x, int y, int w, int h);
ResourceHandle    gpuRenderTargetResource(RenderTarget* rt);
u64               gpuRenderTargetBindIndex(RenderTarget* rt);
void              gpuResolve(RenderTarget* source, RenderTarget* dest);

// Render loop
void              gpuBeginRenderTick();
void              gpuEndRenderTick();
void              gpuSetRaytracingPipeline(PipelineStateHandle h);
void              gpuDispatchRays();
void              gpuSetPipelineState(PipelineStateHandle pso);
void              gpuSetGraphicsConstantSlot(int slot, ResourceHandle resource);
void              gpuSetComputeConstantSlot(int slot, ResourceHandle resource);
void              gpuSetVertexAndIndexBuffers(ResourceHandle vertexBuffer, u64 vertexBytes, ResourceHandle indexBuffer, u64 indexBytes);
void              gpuDrawIndexed(u64 numIndices);

// Debug/Profile markers
void              gpuBeginMarker(char* label);
void              gpuEndMarker();

// Globals
u64 gpuGlobalSize();
void gpuGlobalSet(u8* ptr);

// ================================
// World Render API (Higher level rendering)
// ================================

enum ShaderId
{
   Shader_Raytracing,
   Shader_MeshVS,
   Shader_MeshPS,
   Shader_ShadowmapVS,
   Shader_ShadowmapPS,
   Shader_UIVS,
   Shader_UIPS,
   Shader_SDFRaymarchVS,
   Shader_SDFRaymarchPS,
   Shader_SDFComputeClear,
   Shader_SDFComputeUpdate,
   Shader_PostprocVS,
   Shader_PostprocPS,

   Shader_Count,
};

struct ShaderHeader
{
   uint64_t numBytes;
   uint64_t hash;
   uint64_t offset;
};

struct ShaderCode
{
   u32 numBytes;

   ShaderHeader headers[Shader_Count];

   u8 bytes[];
};

struct Mesh;

struct MeshRenderHandle
{
   MaterialHandle materialHandle;
   BLASHandle blasHandle;
   u64 renderMeshIdx;
   u64 transformIdx;
   ResourceHandle* sShadowResources;
};

struct BlobRenderHandle
{
   MaterialHandle materialHandle;
   ResourceHandle constantResource;
   u64 transformIdx;
};


struct Blob
{
   struct Edit
   {
      BlobEditType type;
      union
      {
         struct  // BlobEdit_Sphere
         {
            float radius;  // It's a sphere at the origin
            vec3 center;
         };
      };
   };
   u32 numEdits;
   Edit edits[gKnobs.maxEdits];

   BlobRenderHandle renderHandle;
};

// Util
vec4 screenToWorld(Camera* cam, float x, float y);


// Setup

void              worldRenderInit(Platform* plat);
void              wrDispose();
void              wrResize(int w, int h);

void              setupPipelineStates(ShaderCode* shaderCode);

MaterialHandle    makeMaterial();

// Render tick
void              renderWorld();

// Upload world objects to GPU
MeshRenderHandle  uploadMeshesToGPU(const Mesh* meshes, sz nMeshes, bool withMaterial = true, bool withBLAS = true);
MeshRenderHandle  uploadMeshToGPU(MeshRenderVertex* sVerts, u32* sIndices, bool withMaterial = true, bool withBLAS = true);
MeshRenderHandle  uploadMeshToGPU(const Mesh& mesh, bool withMaterial = true, bool withBLAS = true);
BlobRenderHandle  uploadBlobToGPU(const Blob& b);

u64               setMeshForDraw(MeshRenderHandle rh);

BLASHandle        getBLAS(ObjectHandle h);

// Lights
void              beginLightingEdit();
LightHandle       addLight();
void              setLight(LightHandle h, LightDescription desc);
void              endLightingEdit();

Material*         materialForObject(ObjectHandle h);
void              materialSetFlag(Material* mat, PSOFlags flag, bool set);

// Globals
u64 worldRenderGlobalSize();
void worldRenderGloalSet(u8* ptr);

// ================================
// Mesh & Blobs
// ================================

struct Mesh
{
   u64 numVerts;
   u64 numIndices;

   vec4* sPositions;
   vec3* sNormals;
   vec2* sTexcoords;
   vec4* sColors;
   u32* sIndices;
};

Mesh makeQuad(f32 cx, f32 cy, f32 w, f32 h, f32 z, vec4 color, Lifetime life, WindingOrder winding = Winding_CW);
Mesh makeQuad(float side, float z, Lifetime life, WindingOrder winding = Winding_CW);
Mesh objLoad(Platform* plat, char* path, Lifetime life);  // TODO: Switch to 3rd party solution.
bool rayTriangleIntersection(vec3 o, vec3 d, vec4* positions, u32* indices, size_t numIndices, float* outT = NULL);

struct LoadedSound
{
   i16* samples;
   int hz;
   int channels;
   int numBytes;
};

// TODO: Separate system for this?
LoadedSound mp3Load(Platform* plat, char* pathToMp3, Lifetime life);


// ================================
// World
// ================================

enum WorldObjectFlag
{
   WorldObject_Mesh = 0x01,
   WorldObject_Blob = 0x02,
   WorldObject_Visible = 0x04,
   WorldObject_CastsShadows = 0x08,
};

struct WorldObject
{
   WorldObjectFlag flags;

   union
   {
      Blob blob;
      Mesh mesh;
   };
};

struct WorldObjectRenderHandle
{
   WorldObjectFlag flags;
   union
   {
      BlobRenderHandle blob;
      MeshRenderHandle mesh;
   };
};

struct World
{
   Camera cam;

   u64 numObjects;
   WorldObject objects[gKnobs.maxObjects];
   WorldObjectRenderHandle renderHandles[gKnobs.maxObjects];
   AABB boundingBoxes[gKnobs.maxObjects];
#if BuildMode(Debug)
   char* debugNames[gKnobs.maxObjects];
#endif

   ObjectHandle currentBlobEdit;
};

struct ObjectIterator;

World*                     getWorld();
World*                     makeAndSetWorld();
void                       setWorld(World*);
mat4                       transformForObject(ObjectHandle h);
void                       setTransformForObject(ObjectHandle h, mat4 transform);
ObjectHandle               addMeshToWorld(Mesh mesh, char* debugName = NULL);
ObjectHandle               newBlob();
Blob*                      beginBlobEdit(ObjectHandle h);
void                       endBlobEdit();
bool                       objectTestFlag(ObjectHandle h, WorldObjectFlag flag);
void                       objectSetFlag(ObjectHandle h, WorldObjectFlag flag, bool set);
u64                        objectIterateCount(WorldObjectFlag type);
ObjectIterator*            objectIterateBegin(WorldObjectFlag type);
bool                       objectIterateHasNext(ObjectIterator*);
ObjectHandle               objectIterateNext(ObjectIterator*);
void                       objectIterateEnd(ObjectIterator*);
WorldObjectRenderHandle*   renderHandleForObject(ObjectHandle oh);
void                       disposeWorld();

// ================================
// UI
// ================================
enum FontSize
{
   FontSize_Tiny,
   FontSize_Small,
   FontSize_Medium,
   FontSize_Big,

   FontSize_Count,

   FontSize_Tall = FontSize_Small,
   FontSize_Grande = FontSize_Medium,
   FontSize_Venti = FontSize_Big,
};

struct Font
{
   Mesh atlasMesh;
   MeshRenderHandle rmesh;
   int atlasSize;
   Texture2D atlasTexture;

   int numChars;
   float fontSizesPx[FontSize_Count];
   void* packedData[FontSize_Count];
};

void           fontInit(Platform* plat, Font* t, char* fontPath, float customSizes[FontSize_Count] = NULL);
void           fontDeinit(Font* t);

void           immInit(Font* t);
void           immKeypress(char k);
void           immMouseMove(i32 x, i32 y);
void           immClick();
void           immRelease();
void           immSetCursor(f32 x, f32 y);
void           immSameLine();
void           immNewFrame();
void           immTextInput(Platform* plat, char** sText, FontSize fsize = FontSize_Small);
void           immList(char** texts, sz numTexts, int highlightedIdx = -1, FontSize fsize = FontSize_Medium);
void           immText(char* text, FontSize fsize = FontSize_Small);
ObjectHandle   immObjectPick();
void           immRender();

// Global
u64 immGlobalSize();
void immGlobalSet(u8* ptr);

// ================================
// Editor
// ================================



enum CommandsEnum
{
   #define Command(enum, str) Command_##enum,
      #include "Commands.inl"
   #undef Command

   Command_Count,
};

void commandPerform(Platform* plat, CommandsEnum cmd);

// Globals
u64 editorGlobalSize();
void editorGlobalSet(u8* ptr);

struct Finder
{
   char* sSearchString;

   int selectionIdx;
};

enum MaterialEditorState
{
   MatEd_Pick,
   MatEd_Edit,
};


struct MaterialEditor
{
   MaterialEditorState state;
   ObjectHandle pickedObj;
};

struct Editor
{
   Font defaultFont;

   Finder finder;
   MaterialEditor materialEd;

   Mode mode;
};

Editor* editor();

void modeEnable(Mode mode);
bool modeIs(Mode mode);
bool modeTick(Platform* plat);

// Finder
void     finderExit(Finder* f);
char**   finderComputeResults(Finder* f, Lifetime life, CommandsEnum* selectedCmd = NULL);

// Material Editor
void materialSliders(MaterialConstantsCB* matc);

#if defined(_MSC_VER)
#pragma warning(pop)
#endif