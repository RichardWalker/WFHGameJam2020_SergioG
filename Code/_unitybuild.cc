// Windows/DirectX/C/C++ includes
#pragma warning(push, 1)
#include <d3d12.h>
#include <dxgi1_4.h>
#include <DirectXMath.h>
#include <Xaudio2.h>

#include <stdio.h>

#pragma warning(push, 4)
#include "Engine.h"
#pragma warning(pop)


#define STBDS_FREE(c, ptr)
#define STBDS_REALLOC(c, ptr, sz) reallocateBytesFor3rd(reinterpret_cast<const u8*>(ptr), sz)

#include "3rd/meow_hash_x64_aesni.h"
#include "3rd/minimp3.h"
#include "3rd/minimp3_ex.h"
#include "3rd/stb_ds.h"
#include "3rd/stb_image.h"
#include "3rd/stb_truetype.h"


#if BuildMode(Debug)
   #ifndef _DEBUG
      #define _DEBUG
   #endif
   // TODO: Why does removing this include give an operator new error???
   #include "3rd/WinPixEventRuntime/pix3.h"
#endif

// Unity build

#pragma warning(push, 4)
#pragma warning(disable:4018)  // signed/unsigned mismatch
#pragma warning(disable:4127)  // constant conditional expr
#pragma warning(disable:4201)  //nonstandard nameless structs
#pragma warning(disable:4238)  // class rvalue used as lvalue
#pragma warning(disable:4244)  // double to float
#pragma warning(disable:4267)  // size_t to smaller type
#pragma warning(disable:4305)  // double to float
#pragma warning(disable:4324)  // padding
#pragma warning(disable:4389)  // signed/unsigned mismatch
#pragma warning(disable:4996)  // fopen unsafe

// Good for cleanup ====
#pragma warning(disable:4100)  // unreferenced parameter
#pragma warning(disable:4189)  // Not referenced
// ====


#include "Game.cc"
#include "Tests.cc"
#include "Main.cc"
#include "Memory.cc"
#include "Input.cc"
#include "Math.cc"
#include "Mesh.cc"
#include "OBJLoad.cc"
#include "SoundLoad.cc"
#include "RenderWorld.cc"
#include "RenderUI.cc"
#include "RenderDXCore.cc"
#include "World.cc"
#include "UI.cc"
#include "Commands.cc"
#include "ModeFinder.cc"
#include "ModeMaterialEditor.cc"
#include "AudioXAudio.cc"
#include "Logging.cc"

#pragma warning(pop)  // pragma warning 4



// 3rd party impl.

// TODO: Ability to set the heap to be used by stb libs

#define MINIMP3_IMPLEMENTATION
#include "3rd/minimp3.h"
#include "3rd/minimp3_ex.h"


#define STB_IMAGE_IMPLEMENTATION
#define STBI_MALLOC(sz) reallocateBytesFor3rd(nullptr, sz)
#define STBI_FREE(ptr)
#define STBI_REALLOC(ptr, sz) reallocateBytesFor3rd((const u8*)ptr, sz)
#include "3rd/stb_image.h"


#define STB_DS_IMPLEMENTATION
#include "3rd/stb_ds.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "3rd/stb_truetype.h"
#pragma warning(pop)  // pragma warning 1
