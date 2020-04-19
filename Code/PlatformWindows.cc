#include <Windows.h>
#include <Xaudio2.h>
#include <stdio.h>
#include <Assert.h>

#include "Engine.h"
#include "InputInternal.h"
#include "3rd/stb_ds.h"


LARGE_INTEGER gPerfFrequency;
static bool gRunning = true;
static HINSTANCE gHinst;

Input gInput;

#define ListSpecialKeys() \
   SpecialKey(VK_SHIFT, Key_Shift) \
   SpecialKey(VK_MENU, Key_Alt) \
   SpecialKey(VK_CONTROL, Key_Ctrl) \
   SpecialKey(VK_ESCAPE, Key_Escape) \
   SpecialKey(VK_RETURN, Key_Enter) \
   SpecialKey(VK_TAB, Key_Tab) \
   SpecialKey(VK_UP, Key_Up) \
   SpecialKey(VK_DOWN, Key_Down) \
   SpecialKey(VK_SPACE, Key_Space) \
   SpecialKey(VK_LEFT, Key_Left) \
   SpecialKey(VK_RIGHT, Key_Right)


// Use dedicated GPU.
extern "C"
{
   __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
}

extern "C"
{
   __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

struct PlatformInternal
{
   HWND hwnd;
   i16* samples;
};
static PlatformInternal* gPlatform;

u8*
winAllocBytes(u64 numBytes)
{
   return (u8*)calloc(1, numBytes);
}

static void
error(const char* message)
{
   OutputDebugStringA(message);
   exit(-1);
}

static LARGE_INTEGER
win32GetWallClock()
{
   LARGE_INTEGER count;
   QueryPerformanceCounter(&count);
   return count;
}

void
winConsoleLog(char* msg)
{
   OutputDebugStringA(msg);
}

u64
winGetMicroseconds()
{
   LARGE_INTEGER wall = win32GetWallClock();
   double freq = (double)gPerfFrequency.QuadPart / (1000 * 1000);

   return wall.QuadPart / freq;
}

void
winEngineQuit()
{
   gRunning = false;
}

LRESULT
wndProc(HWND hwnd, UINT uiMsg, WPARAM wParam, LPARAM lParam)
{
   switch (uiMsg) {
      case WM_CREATE: {
         return TRUE;
      } break;
      case WM_DESTROY: {
         gRunning = false;
         PostQuitMessage(0);
         return 0;
      } break;
      case WM_SIZE: {
         if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED)
         {
            gInput.r.withResize = true;

            gInput.r.width = LOWORD(lParam);
            gInput.r.height = HIWORD(lParam);
         }
      } break;
   }

   return DefWindowProc(hwnd, uiMsg, wParam, lParam);
}

static float win32SecondsElapsed(LARGE_INTEGER now, LARGE_INTEGER then)
{
    i64 counter_elapsed = now.QuadPart - then.QuadPart;

    float sec = (float)counter_elapsed / (float)gPerfFrequency.QuadPart;
    return sec;
}

typedef wchar_t PlatChar;

static char*
winGetPlatformError()
{
   DWORD e = GetLastError();

   char* str = {};

   LPSTR messageBuffer = nullptr;
   size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL, e, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

   return messageBuffer;
}

void
winToPlatStr(const char* str, PlatChar* out, const size_t out_sz)
{
   if ( out && str ) {
      size_t numCharsConverted = 0;
      mbstowcs_s(&numCharsConverted,
       out,
       out_sz / 4,
       str,
       out_sz / sizeof(PlatChar));
   }
}

u64
winFileSizeAscii(char* fname)
{
   u64 bytes = 0;
   FILE* fd = fopen(fname, "rb");
   if (fd) {
      fseek(fd, 0, SEEK_END);
      bytes = ftell(fd);

      fseek(fd, 0, SEEK_SET);
      fclose(fd);
   }
   return bytes;
}

u64
winFileContentsAscii(char* fname, u8*& data, Lifetime life)
{
   u64 bytes = 0;
   FILE* fd = fopen(fname, "rb");
   if (fd) {
      fseek(fd, 0, SEEK_END);
      bytes = ftell(fd);

      fseek(fd, 0, SEEK_SET);

      u64 read = 0;
      // TODO: Figure out what to do with lifetimes here..
      data = winAllocBytes(bytes + 1  /*Adding one for string terminator*/);

      while (read < bytes) {
         read += fread(data + read, 1, bytes - read, fd);
      }
      fclose(fd);
   }

   return bytes;
}

PlatformFnameAtExeAsciiProcDef(winFnameAtExeAscii)
{
   // TODO: Lifetimes...
   char* tmp = (char*)winAllocBytes(len);

   u64 fnameSize = strlen(fname) + 1;
   strcpy(tmp, fname);

   DWORD path_len = GetModuleFileNameA(NULL, fname, (DWORD)len);
   if (path_len + fnameSize > len) {  // Conservative check.. we're counting the exe name even though it's going to be taken out.
      Assert(false); // path too long
   }

   {  // Remove the exe name
      char* last_slash = fname;
      for ( char* iter = fname;
            *iter != '\0';
            ++iter ) {
         if ( *iter == '\\' ) {
            last_slash = iter;
         }
      }
      *(last_slash+1) = '\0';
   }

   strncat(fname, tmp, len);
}


PlatformFnameAtExeProcDef(winFnameAtExe)
{
   // TODO: LIfetime
   PlatChar* tmp = (PlatChar*)winAllocBytes(len);

   wcscpy(tmp, fname);

   DWORD path_len = GetModuleFileNameW(NULL, fname, (DWORD)len);
   if (path_len > len)
   {
      Assert(false); // path too long
   }

   {  // Remove the exe name
      PlatChar* last_slash = fname;
      for ( PlatChar* iter = fname;
            *iter != '\0';
            ++iter ) {
         if ( *iter == '\\' ) {
            last_slash = iter;
         }
      }
      *(last_slash+1) = '\0';
   }

   wcscat(fname, tmp);
}

FILETIME
winLastWriteTime(PlatChar* fname)
{
    FILETIME LastWriteTime = {};

    WIN32_FILE_ATTRIBUTE_DATA Data;
    if(GetFileAttributesExW(fname, GetFileExInfoStandard, &Data))
    {
        LastWriteTime = Data.ftLastWriteTime;
    }

    return(LastWriteTime);
}

void*
winGetWindowHandle()
{
   return (void*)gPlatform->hwnd;
}

void
winGetClientRect(int* w, int* h)
{
   RECT clientRect = {};
   GetClientRect(gPlatform->hwnd, &clientRect);

   *w = clientRect.right;
   *h = clientRect.bottom;
}

Input*
winGetInput()
{
   return &gInput;
}

// Stub game code callbacks
AppInitCallbackProcDef(appInitStub)
{
}

AppTickCallbackProcDef(appTickStub)
{
}

AppDisposeProcDef(appDisposeStub)
{
}

AppLoadShadersProcDef(appLoadShadersStub)
{
}

struct AppMemory
{
   u8 initialized;
   u8* globalPointers[GlobalTable_Count];
   u64 globalSizes[GlobalTable_Count];
};

bool
loadAppCode(wchar_t* dllPath, wchar_t* lockPath, AppCode* app, AppMemory* mem)
{
   bool loaded = false;
   HMODULE enginedll = {};
   VOID* ignored;
   if(!GetFileAttributesExW(lockPath, GetFileExInfoStandard, &ignored)) {
      #if BuildMode(Debug)
         for (int attempt = 0; attempt < 128; ++attempt) {
            wchar_t tmpPath[MaxPath] = {};
            wchar_t pathWithoutExt[MaxPath] = {};
            swprintf(pathWithoutExt, MaxPath, L"%s", dllPath);
            int lastDotIdx = 0;
            for (int i = 0; i < MaxPath; ++i) {
               if (pathWithoutExt[i] == '\0') {
                  break;
               }
               else if (pathWithoutExt[i] == '.') {
                  lastDotIdx = i;
               }
            }
            pathWithoutExt[lastDotIdx] = '\0';

            static int copyIdx = 0;
            copyIdx++;
            copyIdx = copyIdx % 1024;

            swprintf(tmpPath, L"%s%d.dll", pathWithoutExt, copyIdx);

            CopyFileW(dllPath, tmpPath, false);

            enginedll = LoadLibraryW(tmpPath);
            if (enginedll) {
               OutputDebugStringA("Loaded dll");
               OutputDebugStringW(tmpPath);
               loaded = true;
               break;
            }
         }
      #elif BuildMode(Release) // For release, we want to just load up engine.dll
         enginedll = LoadLibraryW(dllPath);
         if (enginedll) {
            OutputDebugStringA("Loaded dll");
            OutputDebugStringW(dllPath);
            loaded = true;
         }
         else {
            OutputDebugStringA("Could not load engine dll!");
         }
      #endif

      GetGlobalSizeTableProc* getGlobalSizeTable = nullptr;
      PatchGlobalTableProc* patchGlobalTable = nullptr;

      if (!enginedll) {
         MessageBoxA(
            NULL,
            "Could not load engine.dll!",
            "Error",
            MB_ICONEXCLAMATION | MB_OK);
         exit(-1);
      }
      else {
         patchGlobalTable = (PatchGlobalTableProc*)GetProcAddress(enginedll, "patchGlobalTable");
         getGlobalSizeTable = (GetGlobalSizeTableProc*)GetProcAddress(enginedll, "getGlobalSizeTable");

         app->appInit = (AppInitProc*)GetProcAddress(enginedll, "appInit");
         app->appTick = (AppTickProc*)GetProcAddress(enginedll, "appTick");
         app->appDispose = (AppDisposeProc*)GetProcAddress(enginedll, "appDispose");
         app->appLoadShaders = (AppLoadShadersProc*)GetProcAddress(enginedll, "appLoadShaders");
      }

      if (!mem->initialized) {
         if (getGlobalSizeTable) {
            getGlobalSizeTable(mem->globalSizes);
         }

         for (int i = 0; i < GlobalTable_Count; ++i) {
            mem->globalPointers[i] = winAllocBytes(mem->globalSizes[i]);
         }

         mem->initialized = 1;
      }

      // Point the dll globals to the right pointers.
      if (patchGlobalTable) {
         patchGlobalTable(mem->globalPointers);
      }
   }
   return loaded;
}

ShaderCode*
getShaderCode(wchar_t* shaderPath)
{
   ShaderCode* sc = NULL;
   FILE* fd = _wfopen(shaderPath, L"rb");
   if (fd) {
      u32 numBytes = 0;

      fread(&numBytes, sizeof(u32), 1, fd);

      // 16 MB is for now a good upper bound.
      if (numBytes > sizeof(ShaderCode) && numBytes < Megabytes(16)) {
         sc = (ShaderCode*)calloc(numBytes, 1);
         fseek(fd, 0, SEEK_SET);

         fread(sc, sizeof(char), numBytes, fd);
      }
      else {
         char msg[1024];
         snprintf(msg, ArrayCount(msg), "I will not allocate %d bytes of shader code.\n", numBytes);
         winConsoleLog(msg);
      }
      fclose(fd);
   }

   return sc;
}

bool
isWhitespace(char c)
{
   return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool
matchPartialParameter(char* a, char* b)
{
   bool match = true;
   while (*a && *b && !isWhitespace(*a) && !isWhitespace(*b)) {
      if (*a != *b) {
         match = false;
         break;
      }
      a++;
      b++;
   }
   return match;
}

int
WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int nShowCmd)
{
   Platform stackPlat = {};
   Platform* plat = &stackPlat;

   plat->fnameAtExeAscii = winFnameAtExeAscii;
   plat->fnameAtExe = winFnameAtExe;
   plat->fileContentsAscii = winFileContentsAscii;
   plat->fileSizeAscii = winFileSizeAscii;
   plat->toPlatStr = winToPlatStr;
   plat->getPlatformError = winGetPlatformError;
   plat->getWindowHandle = winGetWindowHandle;
   plat->engineQuit = winEngineQuit;
   plat->getInput = winGetInput;
   plat->getMicroseconds = winGetMicroseconds;
   plat->consoleLog = winConsoleLog;
   plat->getClientRect = winGetClientRect;

   QueryPerformanceFrequency(&gPerfFrequency);

   WNDCLASS wc;
   wc.style = 0;
   wc.lpfnWndProc = wndProc;
   wc.cbClsExtra = 0;
   wc.cbWndExtra = 0;
   wc.hInstance = gHinst;
   wc.hIcon = NULL;
   wc.hCursor = LoadCursor(NULL, IDC_ARROW);
   wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
   wc.lpszMenuName = NULL;
   wc.lpszClassName = TEXT(gKnobs.windowTitle);

   AppMemory appMemory = {};

   AppCode app = {};
   app.appInit = appInitStub;
   app.appLoadShaders = appLoadShadersStub;
   app.appTick = appTickStub;
   app.appDispose = appDisposeStub;

   // Parse command line.
   char* args = GetCommandLineA();

   bool cmdFullscreen = false;
   RunMode cmdRunMode = gKnobs.defaultRunMode;
   int cmdWidth = gKnobs.width;
   int cmdHeight = gKnobs.height;


   {
      #define SkipNonWhitespace() \
         while(*args && !isWhitespace(*args)) { args++; }

      #define SkipWhitespace() \
         while(*args && isWhitespace(*args)) { args++; }


      // Skip filename
      if(*args++ == '"') { // Quoted path
         while(*args++ != '"') { }
      }
      else {
         SkipNonWhitespace();
      }

      // Note that order matters. We will match any parameter after a whitespace that is a matching prefix.
      while (*args) {
         SkipWhitespace();

         if (matchPartialParameter(args, "-fullscreen")) {
            OutputDebugStringA("Going fullscreen\n");
            cmdFullscreen = true;
         }
         else if (matchPartialParameter(args, "-test")) {
            cmdRunMode = RunMode_Tests;
         }
         else if (matchPartialParameter(args, "-game")) {
            cmdRunMode = RunMode_Game;
         }
         else if (matchPartialParameter(args, "-width")) {
            // Skip rest of the flag
            SkipNonWhitespace();
            SkipWhitespace();

            sscanf(args, "%d", &cmdWidth);
         }
         else if (matchPartialParameter(args, "-height")) {
            // Skip rest of the flag
            SkipNonWhitespace();
            SkipWhitespace();

            sscanf(args, "%d", &cmdHeight);
         }
         else {
            OutputDebugStringA("Unrecognized flag!\n");
         }

         SkipNonWhitespace();
      }
   }

   plat->runMode = cmdRunMode;

   if (RegisterClass(&wc)) {
      if (SUCCEEDED(CoInitialize(NULL))) {
         HWND hwnd = CreateWindow(
            TEXT(gKnobs.windowTitle),                /* Class Name */
            TEXT(gKnobs.windowTitle),                /* Title */
            WS_OVERLAPPEDWINDOW,            /* Style */
            CW_USEDEFAULT, CW_USEDEFAULT,   /* Position */
            cmdWidth, cmdHeight,   /* Size */
            NULL,                           /* Parent */
            NULL,                           /* No menu */
            hinst,                          /* Instance */
            0);                             /* No special parameters */

         RECT clientRect = {};
         GetClientRect(hwnd, &clientRect);

         ShowWindow(hwnd, nShowCmd);

         // Initialize subsystems.


         HDC deviceContext = GetDC(hwnd);

         int monitorRefreshHz = 60;
         {
             int win32RefreshHz = GetDeviceCaps(deviceContext, VREFRESH);
             if (win32RefreshHz > 1)
             {
                 monitorRefreshHz = win32RefreshHz;
             }
         }

         float gameUpdateHz = (monitorRefreshHz / 1.0f);

         gPlatform = (PlatformInternal*)winAllocBytes(sizeof(PlatformInternal));
         gPlatform->hwnd = hwnd;

         PlatChar engineStr[MaxPath] = {};
         PlatChar lockPath[MaxPath] = {};
         PlatChar shaderPath[MaxPath] = {};

         wcscpy(engineStr, L"engine.dll");
         winFnameAtExe(engineStr, MaxPath);

         wcscpy(lockPath, L"lock.tmp");
         winFnameAtExe(lockPath, MaxPath);

         wcscpy(shaderPath, L"shaders.bin");
         winFnameAtExe(shaderPath, MaxPath);

         FILETIME dllTime = winLastWriteTime(engineStr);
         FILETIME shaderTime = winLastWriteTime(shaderPath);

         loadAppCode(engineStr, lockPath, &app, &appMemory);

         app.appInit(plat, cmdFullscreen, cmdWidth, cmdHeight);

         Lifetime shaderCodeLife = Lifetime_User;

         ShaderCode* shaderCode = getShaderCode(shaderPath);

         app.appLoadShaders(shaderCode);

         LARGE_INTEGER flip_wall_clock = win32GetWallClock();
         bool sound_is_valid = false;
         float target_seconds_per_frame = 1.0f / gameUpdateHz;

         while (gRunning) {
            // In debug mode, check every frame for new code to reload.
            #if BuildMode(Debug)
               if (CompareFileTime(&dllTime, &winLastWriteTime(engineStr)) != 0) {
                  if (loadAppCode(engineStr, lockPath, &app, &appMemory)) {
                     dllTime = winLastWriteTime(engineStr);
                  }
               }
               if (CompareFileTime(&shaderTime, &winLastWriteTime(shaderPath)) != 0) {
                  free(shaderCode);
                  for (int attempt = 0; attempt < 10; ++attempt) {
                     shaderCode = getShaderCode(shaderPath);
                     if (shaderCode) { break; }
                     Sleep(1);
                  }
                  if (shaderCode) {
                      app.appLoadShaders(shaderCode);
                  }
                  else {
                      DebugBreak();
                  }

                  shaderTime = winLastWriteTime(shaderPath);
               }
            #endif  // BuildMode(Debug)

            LARGE_INTEGER beginPerfCount = {};
            QueryPerformanceCounter(&beginPerfCount);

            MSG msg = {};

            while (PeekMessage(&msg, hwnd, 0, 0, PM_REMOVE)) {
               switch (msg.message) {
                  case WM_KEYDOWN: {
                     u32 vKeyCode = msg.wParam;
                     bool justPressed = (msg.lParam & (1 << 30)) == 0;

                     // Key presses.
                     if (vKeyCode >= 0x41/*a*/ && vKeyCode <= 0x5A/*z*/) {
                        u32 k = Key_a + vKeyCode - 0x41;
                        gInput.k.special[k] = true;
                        gInput.k.specialTransition[k] = justPressed;
                     }

                     #define SpecialKey(vk, key) \
                        if (vKeyCode == vk) { \
                           gInput.k.special[key] = true;                   \
                           gInput.k.specialTransition[key] = justPressed;  \
                        }

                     ListSpecialKeys()

                     #undef SpecialKey
                  } break;
                  case WM_KEYUP: {
                     u32 vKeyCode = msg.wParam;

                     if (vKeyCode >= 0x41/*a*/ && vKeyCode <= 0x5A/*z*/) {
                        u32 k = Key_a + vKeyCode - 0x41;
                        gInput.k.special[k] = false;
                        gInput.k.specialTransition[k] = true;
                     }

                     #define SpecialKey(keyCodeVal, enumVal) \
                        if (vKeyCode == keyCodeVal) { \
                           gInput.k.special[enumVal] = false; \
                           gInput.k.specialTransition[enumVal] = true; \
                        }
                     ListSpecialKeys();
                     #undef SpecialKey
                  } break;
                  case WM_MOUSEMOVE: {
                     i16 x = LOWORD(msg.lParam);
                     i16 y = HIWORD(msg.lParam);
                     if (gInput.m.numMouseMove < MaxFrameMouseEvents) {
                        int i = gInput.m.numMouseMove++;
                        gInput.m.mouseMove[i].x = x;
                        gInput.m.mouseMove[i].y = y;
                     }
                  } break;
                  case WM_LBUTTONDOWN: {
                     if (gInput.m.numClicks < MaxFrameMouseEvents) {
                        int i = gInput.m.numClicks++;
                        gInput.m.clicks[i].left = MouseDown;
                     }
                     SetCapture(hwnd);
                  } break;
                  case WM_LBUTTONUP: {
                     if (gInput.m.numClicks < MaxFrameMouseEvents) {
                        int i = gInput.m.numClicks++;
                        gInput.m.clicks[i].left = MouseUp;
                     }
                     ReleaseCapture();
                  } break;
                  case WM_RBUTTONDOWN: {
                     if (gInput.m.numClicks < MaxFrameMouseEvents) {
                        int i = gInput.m.numClicks++;
                        gInput.m.clicks[i].right = MouseDown;
                     }
                     SetCapture(hwnd);
                  } break;
                  case WM_RBUTTONUP: {
                     if (gInput.m.numClicks < MaxFrameMouseEvents) {
                        int i = gInput.m.numClicks++;
                        gInput.m.clicks[i].right = MouseUp;
                     }
                     ReleaseCapture();
                  } break;
                  default: {
                     TranslateMessage(&msg);
                     DispatchMessage(&msg);
                  }
               }
            }


            ShowCursor(false);  // TODO: JUST FOR THE GAME JAM.

            app.appTick(plat);

            if (GetFocus() == winGetWindowHandle()) {// TODO: JUST FOR THE GAME JAM.
               RECT r = {};
               GetWindowRect((HWND)winGetWindowHandle(), &r);
               SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);

               plat->windowRectL = r.left;  // TODO: GAME JAM HACK
               plat->windowRectR = r.right;  // TODO: GAME JAM HACK
               plat->windowRectT = r.top;  // TODO: GAME JAM HACK
               plat->windowRectB = r.bottom;  // TODO: GAME JAM HACK
            }

            // Clear key transitions.
            memset(gInput.k.specialTransition, 0, ArrayCount(gInput.k.specialTransition) * sizeof(*gInput.k.specialTransition));

            // Clear mouse input.
            memset(&gInput.m, 0, sizeof(gInput.m));

            // Clear resize
            memset(&gInput.r, 0, sizeof(gInput.r));

            flip_wall_clock = win32GetWallClock();
         }
      }
      app.appDispose(plat);
   }

   return 0;
}