@echo off
if not exist Build mkdir Build

pushd Build

del *.pdb > NUL 2> NUL
taskkill /F /IM demo.exe > NUL 2> NUL

set CompilerFlags=/O2 /I ..\ /I ..\Code /FS /Zf
rem set CompilerFlags=/Od /Zi /I ..\ /I ..\Code /FS /Zf


echo %Defines%

REM If set to true, build.bat will try and use clang-cl.exe, if it finds it in the PATH.
set UseClang=false

set Compiler=cl
set Warnings=

where /q clang-cl
if "%errorlevel%"=="0" (
   if "%UseClang%"=="true" (
      set Compiler=clang-cl
      set Warnings=-maes -mssse3 -Od -Wno-address-of-temporary -Wno-c++11-narrowing -Wno-writable-strings
   )
)
@echo on
@echo Use clang? %UseClang%
@echo Compiler? %Compiler%
@echo off

@echo ==== Building executable... ====
REM Running asynchronously to cut some build time.
start /b %Compiler% %CompilerFlags% ..\Code\_unitybuild_win32.cc ^
   %Warnings% ^
   /MT /Fegame.exe /Fdgame%random%.pdb User32.Lib Ole32.Lib dxgi.lib Gdi32.Lib ^
   /link /incremental:no

@echo ==== Building engine and game... ====
echo Waiting for pdb > lock.tmp
rem ========== DLL =========
%Compiler% %CompilerFlags% ..\Code\_unitybuild.cc ^
   %Warnings% ^
   /LDd /Feengine /Fdengine%random%.pdb d3d12.lib dxgi.lib  ^
   /link /incremental:no /pdb:engine%random%.pdb
del lock.tmp


@echo ==== Building PIX interface... ====
rem ========== DLL =========
start /b %Compiler% %CompilerFlags% ..\Code\PIXInterface.cc ^
   %Warnings% ^
   /MT /LDd /FePIXinterface d3d12.lib dxgi.lib ..\3rd\WinPixEventRuntime\x64\WinPixEventRuntime.lib ^
   /link /incremental:no /pdb:PIX%random%.pdb

REM Building shader compiler in this process because we run it right after.
@echo ==== Building shader compiler ====
%Compiler% %CompilerFlags% ..\Code\_unitybuild_shaders.cc  ^
  %Warnings% ^
  /MT /Feshaders /Fdshaders%random%.pdb d3dcompiler.lib ^
  /link /incremental:no


copy ..\3rd\WinPixEventRuntime\x64\WinPixEventRuntime.dll WinPixEventRuntime.dll

popd

@echo ==== Building shaders ====
if exist build\shaders.exe (
  build\shaders
)