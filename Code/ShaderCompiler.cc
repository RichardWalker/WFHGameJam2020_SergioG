#include "Engine.h"

#include <Windows.h>
#include <dxcapi.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <inttypes.h>

#define STB_DS_IMPLEMENTATION
#include "3rd/stb_ds.h"

#define OffsetOf(s, m) ( (size_t)&((s*)0)->m )
#if Debug
   // #define ShaderFlags (D3DCOMPILE_DEBUG | D3DCOMPILE_ALL_RESOURCES_BOUND | D3DCOMPILE_SKIP_OPTIMIZATION)
   #define ShaderFlags (D3DCOMPILE_DEBUG | D3DCOMPILE_ALL_RESOURCES_BOUND)
#else
   #define ShaderFlags (D3DCOMPILE_ALL_RESOURCES_BOUND)
#endif

static DxcCreateInstanceProc CreateInstance;

// List of shaders.

enum ShaderType {
   ShaderType_Raytracing,
   ShaderType_Vertex,
   ShaderType_Pixel,
   ShaderType_Compute,
};

wchar_t* gShaderPaths[] = {
   /*Shader_Raytracing*/L"Shaders/Raytracing.hlsl",
   /*Shader_MeshVS*/L"Shaders/Mesh.hlsl",
   /*Shader_MeshPS*/L"Shaders/Mesh.hlsl",
   /*Shader_ShadowmapVS*/L"Shaders/Shadowmap.hlsl",
   /*Shader_ShadowmapPS*/L"Shaders/Shadowmap.hlsl",
   /*Shader_UIVS*/L"Shaders/UI.hlsl",
   /*Shader_UIPS*/L"Shaders/UI.hlsl",
   /*Shader_SDFRaymarchVS*/L"Shaders/SDFRaymarch.hlsl",
   /*Shader_SDFRaymarchPS*/L"Shaders/SDFRaymarch.hlsl",
   /*Shader_SDFComputeClear*/L"Shaders/SDF.hlsl",
   /*Shader_SDFComputeUpdate*/L"Shaders/SDF.hlsl",
   /*Shader_PostprocVS*/L"Shaders/Postproc.hlsl",
   /*Shader_PostprocPS*/L"Shaders/Postproc.hlsl",
};

ShaderType gShaderTypes[] = {
   /*Shader_Raytracing*/ShaderType_Raytracing,
   /*Shader_Mesh*/ShaderType_Vertex,
   /*Shader_Mesh*/ShaderType_Pixel,
   /*Shader_ShadowNULLmap*/ShaderType_Vertex,
   /*Shader_ShadowNULLmap*/ShaderType_Pixel,
   /*Shader_UI*/ShaderType_Vertex,
   /*Shader_UI*/ShaderType_Pixel,
   /*Shader_SDFRaymarch*/ShaderType_Vertex,
   /*Shader_SDFRaymarch*/ShaderType_Pixel,
   /*Shader_SDFComputeClear*/ShaderType_Compute,
   /*Shader_SDFComputeUpdate*/ShaderType_Compute,
   /*Shader_PostprocVS*/ShaderType_Vertex,
   /*Shader_PostprocPS*/ShaderType_Pixel,
};

// Only for compute entryPoints.
static wchar_t* gEntryPoints[] = {
   /*Shader_Raytracing*/NULL,
   /*Shader_Mesh*/NULL,
   /*Shader_Mesh*/NULL,
   /*Shader_ShadowNULLmap*/NULL,
   /*Shader_ShadowNULLmap*/NULL,
   /*Shader_UI*/NULL,
   /*Shader_UI*/NULL,
   /*Shader_SDFRNULLaymarch*/NULL,
   /*Shader_SDFRNULLaymarch*/NULL,
   /*Shader_SDFComputeClear*/L"clear",
   /*Shader_SDFComputeUpdate*/L"updateForBlob",
   /*Shader_PostprocVS*/NULL,
   /*Shader_PostprocPS*/NULL,
};

bool
readFileContentsW(wchar_t* path, char** outBytes, u64* outSize)
{
   bool ok = false;
   FILE* fd = _wfopen(path, L"rb");
   if (fd) {
      fseek(fd, 0, SEEK_END);
      *outSize = ftell(fd);
      if (*outSize) {
         fseek(fd, 0, SEEK_SET);
         *outBytes = (char*)calloc(1, *outSize);
         u64 read = 0;
         while (read < *outSize) {
            read += fread(*outBytes + read, 1, *outSize - read, fd);
         }
         ok = true;
      }
   }
   return ok;
}

bool
readFileContentsA(char* path, char** outBytes, u64* outSize)
{
   bool ok = false;
   FILE* fd = fopen(path, "rb");
   if (fd) {
      fseek(fd, 0, SEEK_END);
      *outSize = ftell(fd);
      if (*outSize) {
         fseek(fd, 0, SEEK_SET);
         *outBytes = (char*)calloc(1, *outSize);
         u64 read = 0;
         while (read < *outSize) {
            read += fread(*outBytes + read, 1, *outSize - read, fd);
         }
         ok = true;
      }
   }
   return ok;
}

struct ShaderIncludes : ID3DInclude
{
   virtual HRESULT Open(D3D_INCLUDE_TYPE, LPCSTR fileName, LPCVOID, LPCVOID * ppData, UINT* outBytes)
   {
      char shaderName[128] = {};

      snprintf(shaderName, ArrayCount(shaderName), "Shaders/%s", fileName);

      char* data = {};
      u64 nBytes = 0;
      if (!readFileContentsA(shaderName, &data, &nBytes)){
         return E_FAIL;
      }

      *outBytes = nBytes;
      *ppData = data;

      return nBytes ? S_OK : E_FAIL;
   }

   virtual HRESULT Close(LPCVOID)
   {
      return 0;
   }
};

static ShaderIncludes gShaderIncludes;

IDxcCompiler* gDxcCompiler = 0;
IDxcLibrary*  gDxcLibrary = 0;



IDxcBlob*
compileShaderDXIL(wchar_t* pathToShader, ShaderType type, wchar_t* computeEntryPoint)
{
   HRESULT hr;
   UINT32 code =0;
   IDxcBlobEncoding* shaderText(nullptr);

   // Load and encode the shader file
   hr = gDxcLibrary->CreateBlobFromFile(pathToShader, &code, &shaderText);
   if (!SUCCEEDED(hr)) {
      fprintf(stderr, "Could not create blob from file\n");
      exit(-1);
   }

   IDxcIncludeHandler* dxcIncludeHandler;
   hr = gDxcLibrary->CreateIncludeHandler(&dxcIncludeHandler);
   if (!SUCCEEDED(hr)) {
      fprintf(stderr, "Could not create blob from file\n");
      exit(-1);
   }


   wchar_t* args[] = {
      L"/Zi"
   };

   // Compile the shader
   IDxcOperationResult* result;
   if (type == ShaderType_Raytracing) {
      hr = gDxcCompiler->Compile(
         shaderText,
         pathToShader,
         L"",  // entrypoint
         L"lib_6_3",  // target profile
         (LPCWSTR*)args,  // arguments
         ArrayCount(args),  // arg count
         nullptr,   // defines
         0,   // defines count
         dxcIncludeHandler,
         &result);
   }
   else if (type == ShaderType_Vertex) {
      hr = gDxcCompiler->Compile(
       shaderText,
       pathToShader,
       L"vertexMain",  // entrypoint
       L"vs_6_0",  // target profile
       (LPCWSTR*)args,  // arguments
       ArrayCount(args),  // arg count
       nullptr,   // defines
       0,   // defines count
       dxcIncludeHandler,
       &result);
   }
   else if (type == ShaderType_Pixel) {
      hr = gDxcCompiler->Compile(
       shaderText,
       pathToShader,
       L"pixelMain",  // entrypoint
       L"ps_6_0",  // target profile
       (LPCWSTR*)args,  // arguments
       ArrayCount(args),  // arg count
       nullptr,   // defines
       0,   // defines count
       dxcIncludeHandler,
       &result);
   }
   else if (type == ShaderType_Compute) {
      hr = gDxcCompiler->Compile(
       shaderText,
       pathToShader,
       computeEntryPoint,  // entrypoint
       L"cs_6_0",  // target profile
       (LPCWSTR*)args,  // arguments
       ArrayCount(args),  // arg count
       nullptr,   // defines
       0,   // defines count
       dxcIncludeHandler,
       &result);
   }

   if (!SUCCEEDED(hr)) {
      fprintf(stderr, "Could not run compilation\n");
      exit(-1);
   }

   result->GetStatus(&hr);

   if (!SUCCEEDED(hr)) {
      IDxcBlobEncoding* error;
      result->GetErrorBuffer(&error);

      char* str = (char*)error->GetBufferPointer();

      fprintf(stderr, "%s\n", str);
      exit(-1);
   }

   IDxcBlob* blob = 0;
   result->GetResult(&blob);

   return blob;
}

ID3DBlob*
compileShaderLegacy(wchar_t* pathToShader, ShaderType type, wchar_t* computeEntryPoint)
{
   // Shader source
   ID3DBlob* byteCode = NULL;
   ID3DBlob* errors = NULL;

   char* source = {};
   u64 size = 0;
   if (!readFileContentsW(pathToShader, &source, &size)) {
      fwprintf(stderr, L"Could not read file. %s\n", pathToShader);
   }

   char* entryPoint;
   char* profile;

   if (type == ShaderType_Vertex) {
      entryPoint = "vertexMain";
      profile  = "vs_5_1";
   }
   else if (type == ShaderType_Pixel) {
      entryPoint = "pixelMain";
      profile = "ps_5_1";
   } else if (type ==ShaderType_Compute) {
      profile = "cs_5_1";
      // Ugh...
      static char tmp[128] = {};
      int i = 0;
      do {
         Assert(computeEntryPoint[i] < 128);
         char val = computeEntryPoint[i];
         tmp[i] = val;
         i++;
      } while (computeEntryPoint[i-1] != '\0');

      entryPoint = tmp;
   }

   if (!SUCCEEDED(D3DCompileFromFile(pathToShader,
             NULL, // _In_reads_opt_(_Inexpressible_(pDefines->Name != NULL)) CONST D3D_SHADER_MACRO* pDefines,
             &gShaderIncludes,  // _In_opt_ ID3DInclude* pInclude
             entryPoint, // _In_ LPCSTR pEntrypoint,
             profile, // _In_ LPCSTR pTarget,
             ShaderFlags,
             0, //_In_ UINT Flags2,
             &byteCode, //_Out_ ID3DBlob** ppCode,
             &errors))) {
      const char* errorText = static_cast<const char*>(errors->GetBufferPointer());
      fprintf(stderr, "%s\n", errorText);
   }

   return byteCode;
}

int main()
{
   Assert(ArrayCount(gShaderPaths) == Shader_Count);
   Assert(ArrayCount(gShaderTypes) == Shader_Count);

   size_t sizeMinusContents = OffsetOf(ShaderCode, bytes);
   ShaderCode* res = (ShaderCode*)calloc(sizeMinusContents, 1);
   uint8_t* contents = {};


   // Load shader compiler
   {
      HMODULE dll = LoadLibraryA("dxcompiler.dll");
      if (!dll) {
         fprintf(stderr, "Could not load dll\n");
      }
      else {
         CreateInstance = (DxcCreateInstanceProc)GetProcAddress(dll, "DxcCreateInstance");
         if (!CreateInstance) {
            fprintf(stderr, "Could not load procedure\n");
         }
         else {
            if (!SUCCEEDED(CreateInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler), (LPVOID*)&gDxcCompiler)) ||
                !SUCCEEDED(CreateInstance(CLSID_DxcLibrary, __uuidof(IDxcLibrary), (LPVOID*)&gDxcLibrary))) {
               fprintf(stderr, "Could not create instance\n");
            }
         }
      }
   }

   uint64_t numContentBytes = 0;

   bool alwaysUseDxil = false;

   for (int shaderIdx = 0; shaderIdx < Shader_Count; ++shaderIdx) {
      wchar_t* pathToShader = gShaderPaths[shaderIdx];
      ShaderType type = gShaderTypes[shaderIdx];
      wchar_t* entryPoint = gEntryPoints[shaderIdx];

      uint8_t* bytes = nullptr;
      uint64_t numBytes = 0;

      if (alwaysUseDxil || type == ShaderType_Raytracing) {
         IDxcBlob* blob = compileShaderDXIL(pathToShader, type, entryPoint);
         bytes = (uint8_t*)blob->GetBufferPointer();
         numBytes = blob->GetBufferSize();
      }
      else {
         ID3DBlob* blob = compileShaderLegacy(pathToShader, type, entryPoint);
         bytes = (uint8_t*)blob->GetBufferPointer();
         numBytes = blob->GetBufferSize();
      }


      res->headers[shaderIdx].numBytes = numBytes;
      res->headers[shaderIdx].offset = numContentBytes;
      res->headers[shaderIdx].hash = 0;  // TODO: hash
      // Append contents
      arrsetlen(contents, arrlen(contents) + numBytes);
      memcpy(contents + numContentBytes, bytes, numBytes);

      numContentBytes += numBytes;
   }

   res->numBytes = sizeMinusContents + numContentBytes;

   FILE* fd = fopen("Build/shaders_tmp.bin", "wb");
   if (!fd) {
      fprintf(stderr, "Could not open output file for writing.\n");
   }
   else {
      size_t written = 0;
      do {
         written = fwrite(res, sizeMinusContents, 1, fd);
      } while (written == 0);

      written = 0;
      do {
         written = fwrite(contents, numContentBytes, 1, fd);
      } while (written == 0);

      fclose(fd);
   }

   MoveFileExA(
     "Build/shaders_tmp.bin",
     "Build/shaders.bin",
     MOVEFILE_REPLACE_EXISTING
   );

   fprintf(stderr, "Done building shaders.\n");
}