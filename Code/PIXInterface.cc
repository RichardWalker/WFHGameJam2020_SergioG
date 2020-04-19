#include <Windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>
#ifndef _DEBUG
   #define _DEBUG
#endif
#include "3rd/WinPixEventRuntime/pix3.h"

#include "Engine.h"

Export void
beginMarker(void* cmdList, UINT64 color, PCSTR str)
{
   PIXBeginEvent((ID3D12GraphicsCommandList*)cmdList, color, str);
}

Export void
endMarker(void* cmdList)
{
   PIXEndEvent((ID3D12GraphicsCommandList*)cmdList);
}