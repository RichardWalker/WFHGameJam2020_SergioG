#if BuildMode(Debug)
#include <DXProgrammableCapture.h>
#endif

#define DX12(expr) do { if (!SUCCEEDED(expr)) { handleDx12Error(#expr); } } while(0)
#if BuildMode(Debug)
   // #define ShaderFlags (D3DCOMPILE_DEBUG | D3DCOMPILE_ALL_RESOURCES_BOUND | D3DCOMPILE_SKIP_OPTIMIZATION)
   #define ShaderFlags (D3DCOMPILE_DEBUG | D3DCOMPILE_ALL_RESOURCES_BOUND)
#else
   #define ShaderFlags (D3DCOMPILE_ALL_RESOURCES_BOUND)
#endif

struct TLAS
{
   ResourceHandle instancesRes;
   D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS asIn;
};

struct BLAS
{
   ResourceHandle blasRes;
};

struct RaytracingPSO
{
   ID3D12StateObject* obj;
   ID3D12Resource* shaderTable;
   u64 recordSize;
   u64 recordTableSize;
   u64 numHitGroups;
   u64 numMissShaders;
};

struct RenderTarget
{
   // ID3D12Resource* resource;
   ResourceHandle resourceHandle;

   D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle;

   RTFlags creationFlags;
   u64 bindIndexSRV;
   u64 bindIndexUAV;
};

static struct Renderer
{
   // Public
   i32 fbWidth;
   i32 fbHeight;
   u64 frameCount;
   u64 frameIdxModulo;  // 0 or 1

   // Private
   u8 privateByte;  // Used to assure that the public interface above matches the RendererI struct.

   // Render targets
   // RenderTarget* rtColor[gKnobs.swapChainBufferCount];
   RenderTarget* rtDepth[gKnobs.swapChainBufferCount];
   RenderTarget* rtResolve[gKnobs.swapChainBufferCount];

   ID3D12Device* device;
   IDXGISwapChain3* swapChain;
   ID3D12Resource* backBufferRes[gKnobs.swapChainBufferCount];  // TODO: We keep a resource handle in render target. Delete this?

   ID3D12RootSignature* graphicsRootSignature;
   ID3D12RootSignature* computeRootSignature;

   ID3D12CommandAllocator* commandAllocator[gKnobs.swapChainBufferCount];
   ID3D12CommandQueue* commandQueue;
   ID3D12GraphicsCommandList4* commandList;

   ID3D12PipelineState** sPSOs;
   RaytracingPSO* sDxrPSOs;

   ID3D12Resource** sResource;

   // Upload heap
   ID3D12Resource* uploadHeaps[gKnobs.swapChainBufferCount];
   u8* uploadHeapPtr;
   sz uploadHeapUsedBytes;

   vec3 clearColor;

   ID3D12DescriptorHeap* rtvHeap;
   ID3D12DescriptorHeap* dsvHeap;
   u64 rtvDescCount;
   u64 dsvDescCount;

   ID3D12DescriptorHeap* grvHeap;
   u64 srvDescCount;
   u64 uavDescCount;
   u64 cbvDescCount;

   HANDLE frameFenceEvent = {};
   ID3D12Fence* frameFence;

   // Ray tracing
   ID3D12Device5* dxrDevice;
   u64 bvhDescriptorIdx;
   PipelineStateHandle dxrCurrentPipeline;

   ID3D12RootSignature* dxrRootSignature;

   BLAS* sBLAS;

   struct ToFree
   {
      ResourceHandle h;
      u64 frameInTheFuture;
   };
   ToFree* sToFree;
   ResourceHandle* sFreeResHandles;
   D3D12_CPU_DESCRIPTOR_HANDLE* sFreeRTVHandles;
   D3D12_CPU_DESCRIPTOR_HANDLE* sFreeDSVHandles;

   ResourceHandle* sAppResources;  // Live forever. Tracked for debug purposes.

   Platform* plat;

} *gRenderCore;

#if BuildMode(Debug)

void beginCapture()
{
   IDXGraphicsAnalysis* ga;
   DXGIGetDebugInterface1(0, IID_PPV_ARGS(&ga));
   if (ga) {
      ga->BeginCapture();
   }
}

void endCapture()
{
   IDXGraphicsAnalysis* ga;
   DXGIGetDebugInterface1(0, IID_PPV_ARGS(&ga));
   if (ga) {
      ga->EndCapture();
   }
}

struct PixApi
{
   void (*BeginEventProc)(void*, UINT64, _In_ PCSTR);
   void (*EndEventProc)(void*);
} gPixApi;

void
gpuPixInit(Platform* p)
{
   char path[MaxPath] = {};
   snprintf(path, MaxPath, "PIXinterface.dll");
   p->fnameAtExeAscii(path, MaxPath);

   HMODULE dll = LoadLibraryA(path);
   if (dll) {
      gPixApi.BeginEventProc = (decltype(gPixApi.BeginEventProc))GetProcAddress(dll, "beginMarker");
      gPixApi.EndEventProc = (decltype(gPixApi.EndEventProc))GetProcAddress(dll, "endMarker");
   }
}

void
gpuBeginMarker(char* name)
{
   Renderer* r = gRenderCore;
   if (gPixApi.BeginEventProc)  gPixApi.BeginEventProc(r->commandList, PIX_COLOR(0,0,255), name);
}

void
gpuEndMarker()
{
   Renderer* r = gRenderCore;
   if (gPixApi.EndEventProc) gPixApi.EndEventProc(r->commandList);
}

#else

void gpuPixInit(Platform* p) { }
void gpuBeginMarker(char* ) { }
void gpuEndMarker() { }

#endif

u64
gpuGlobalSize()
{
   return sizeof(Renderer);
}

void
gpuGlobalSet(u8* ptr)
{
   gRenderCore = (Renderer*)ptr;
}

D3D12_RESOURCE_STATES toDXState[ResourceState_Count] =
{
   /*ResourceState_GenericRead*/ D3D12_RESOURCE_STATE_GENERIC_READ,
   /*ResourceState_CopyDest*/ D3D12_RESOURCE_STATE_COPY_DEST,
   /*ResourceState_VertexBuffer*/ D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
   /*ResourceState_IndexBuffer*/ D3D12_RESOURCE_STATE_INDEX_BUFFER,
   /*ResourceState_PixelShaderResource*/ D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
   /*ResourceState_RenderTarget*/ D3D12_RESOURCE_STATE_RENDER_TARGET,
   /*ResourceState_DepthWrite*/ D3D12_RESOURCE_STATE_DEPTH_WRITE,
   /*ResourceState_NonPixelShader*/D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
   /*ResourceState_UnorderedAccess*/D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
   /*ResourceState_AccelerationStructure*/D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
   /*ResourceState_ResolveSource*/D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
   /*ResourceState_ResolveDest*/D3D12_RESOURCE_STATE_RESOLVE_DEST,
   /*ResourceState_Present*/D3D12_RESOURCE_STATE_PRESENT,
};

D3D12_HEAP_TYPE toDXHeapType[GPUHeapType_Count] =
{
   /*GPUHeapType_Default*/ D3D12_HEAP_TYPE_DEFAULT,
   /*GPUHeapType_Upload*/ D3D12_HEAP_TYPE_UPLOAD,
};

RendererI*
gpu()
{
   Assert (OffsetOf(Renderer, privateByte) == sizeof(RendererI));
   return reinterpret_cast<RendererI*>(gRenderCore);
}

void
handleDx12Error(const char* exprStr)
{
   logMsg("DX expression failed: [ %s ]", exprStr);  // TODO: M

   ExitProcess(1);
}

D3D12_HEAP_PROPERTIES
heapProperties( D3D12_HEAP_TYPE type )
{
   D3D12_HEAP_PROPERTIES heapProperties = {};

   heapProperties.Type = type;
   heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
   heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
   heapProperties.VisibleNodeMask = 1;
   heapProperties.CreationNodeMask = 1;

   return heapProperties;
}

D3D12_RESOURCE_DESC
resourceDescForTexture2D( const DXGI_FORMAT format, const UINT width, const UINT height , D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
{
   D3D12_RESOURCE_DESC desc = {};

   {
      desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      desc.Alignment = 0;
      desc.Width = width;
      desc.Height = height;
      desc.DepthOrArraySize = 1;
      desc.MipLevels = 1;
      desc.Format = format;
      desc.SampleDesc.Count = 1;
      desc.SampleDesc.Quality = 0;
      desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
      desc.Flags = flags;
   }

   return desc;
}

D3D12_RESOURCE_DESC
resourceDescForTexture3D( const DXGI_FORMAT format, const UINT width, const UINT height, const UINT depth, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
{
   D3D12_RESOURCE_DESC desc = {};

   {
      desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
      desc.Alignment = 0;
      desc.Width = width;
      desc.Height = height;
      desc.DepthOrArraySize = static_cast<UINT16>(depth);
      desc.MipLevels = log2(Min(width, Min(height, depth)));
      desc.Format = format;
      desc.SampleDesc.Count = 1;
      desc.SampleDesc.Quality = 0;
      desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
      desc.Flags = flags;
   }

   return desc;
}

D3D12_RESOURCE_DESC
resourceDescForTextureCube( const DXGI_FORMAT format, const UINT width, const UINT height, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
{
   D3D12_RESOURCE_DESC desc = {};

   {
      desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      desc.Alignment = 0;
      desc.Width = width;
      desc.Height = height;
      desc.DepthOrArraySize = 6;
      desc.MipLevels = log2(Min(width, height));
      desc.Format = format;
      desc.SampleDesc.Count = 1;
      desc.SampleDesc.Quality = 0;
      desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
      desc.Flags = flags;
   }

   return desc;
}

D3D12_RESOURCE_DESC
resourceDescForTexture2DMS( const DXGI_FORMAT format, const UINT width, const UINT height, const UINT sampleCount, const D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE )
{
   D3D12_RESOURCE_DESC desc = {};

   {
      desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      desc.Alignment = 0;
      desc.Width = width;
      desc.Height = height;
      desc.DepthOrArraySize = 1;
      desc.MipLevels = 1;
      desc.Format = format;
      desc.SampleDesc.Count = sampleCount;
      desc.SampleDesc.Quality = 0;
      desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
      desc.Flags = flags;
   }

   return desc;
}

D3D12_RESOURCE_DESC
resourceDescForBuffer( UINT size, UINT alignment, const D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
{
   D3D12_RESOURCE_DESC desc = {};

   {
      desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      desc.Alignment = alignment;
      desc.Width = size;
      desc.Height = 1;
      desc.DepthOrArraySize = 1;
      desc.MipLevels = 1;
      desc.Format = DXGI_FORMAT_UNKNOWN;
      desc.SampleDesc.Count = 1;
      desc.SampleDesc.Quality = 0;
      desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      desc.Flags = flags;
   }

   return desc;
}

static ID3D12Resource*
getResource(const ResourceHandle h)
{
   Renderer* r = gRenderCore;
   u64 i = (u64)h.idx < arrlen(r->sResource) ? h.idx : 0;

   ID3D12Resource* res = r->sResource[i];

   return res;
}

// DX Internal version
void
gpuBarrierForResource(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
   Renderer* r = gRenderCore;

   // TODO: Batch up barriers
   D3D12_RESOURCE_BARRIER barrier = {};
   {
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      barrier.Transition.pResource = resource;
      barrier.Transition.StateBefore = before;
      barrier.Transition.StateAfter = after;
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
   }
   r->commandList->ResourceBarrier(1, &barrier);
}

void
gpuBarrierForResource(ResourceHandle resource, ResourceState before, ResourceState after)
{
   Renderer* r = gRenderCore;

   // TODO: Batch up barriers
   D3D12_RESOURCE_BARRIER barrier = {};
   {
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      barrier.Transition.pResource = getResource(resource);
      barrier.Transition.StateBefore = toDXState[before];
      barrier.Transition.StateAfter = toDXState[after];
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
   }
   r->commandList->ResourceBarrier(1, &barrier);
}


void
gpuUAVBarrier(ResourceHandle resource)
{
   Renderer* r = gRenderCore;
   D3D12_RESOURCE_BARRIER uavBarrier;
   uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
   uavBarrier.UAV.pResource = getResource(resource);
   uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
   r->commandList->ResourceBarrier(1, &uavBarrier);
}

D3D12_CPU_DESCRIPTOR_HANDLE
gpuGetSRVDescriptorCPU(const u64 idx)
{
   Renderer* r = gRenderCore;

   u64 i = idx < r->srvDescCount ? idx : 0;

   D3D12_CPU_DESCRIPTOR_HANDLE handle = r->grvHeap->GetCPUDescriptorHandleForHeapStart();
   handle.ptr += r->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * i;

   return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE
gpuGetUAVDescriptorCPU(const u64 idx)
{
   Renderer* r = gRenderCore;

   u64 i = idx < r->uavDescCount ? idx : 0;

   D3D12_CPU_DESCRIPTOR_HANDLE handle = r->grvHeap->GetCPUDescriptorHandleForHeapStart();
   handle.ptr += r->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * (gKnobs.numSRVDescriptors + i);

   return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE
gpuGetCBVDescriptorCPU(const u64 idx)
{
   Renderer* r = gRenderCore;

   u64 i = idx < r->cbvDescCount ? idx : 0;

   D3D12_CPU_DESCRIPTOR_HANDLE handle = r->grvHeap->GetCPUDescriptorHandleForHeapStart();
   handle.ptr += r->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * (gKnobs.numSRVDescriptors + gKnobs.numUAVDescriptors + i);

   return handle;
}

u64
gpuAllocateCBVDescriptor(const D3D12_CONSTANT_BUFFER_VIEW_DESC* cbvDesc)
{
   Renderer* r = gRenderCore;

   Assert (r->cbvDescCount < gKnobs.numCBVDescriptors);

   u64 result = r->cbvDescCount++;

   r->device->CreateConstantBufferView(cbvDesc, gpuGetCBVDescriptorCPU(result));

   return result;
}

u64
gpuAllocateSRVDescriptor(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc, ID3D12Resource* resource)
{
   Renderer* r = gRenderCore;

   Assert (r->srvDescCount < gKnobs.numSRVDescriptors);

   u64 result = r->srvDescCount++;

   r->device->CreateShaderResourceView(resource, srvDesc, gpuGetSRVDescriptorCPU(result));

   return result;
}

u64
gpuAllocateUAVDescriptor(D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc, ID3D12Resource* resource)
{
   Renderer* r = gRenderCore;
   Assert (r->uavDescCount < gKnobs.numUAVDescriptors);

   u64 result = r->uavDescCount++;
   r->device->CreateUnorderedAccessView(resource, nullptr, uavDesc, gpuGetUAVDescriptorCPU(result));

   return result;
}

ResourceHandle
gpuPushResource(ID3D12Resource* resource)
{
   Renderer* r = gRenderCore;

   u64 idx = 0;

   u64 numFreeHandles = arrlen(r->sFreeResHandles);
   if (numFreeHandles > 0) {
      idx = arrpop(r->sFreeResHandles).idx;
      u64 newNumFree = arrlen(r->sFreeResHandles);
      r->sResource[idx] = resource;
   }
   else {
      idx = arrlen(r->sResource);
      SBPush(r->sResource, resource, Lifetime_App);
   }
   return ResourceHandle{idx};
}

void
gpuSetResourceDataAtOffset(ResourceHandle resHnd, const void* data, size_t numBytes, const sz offset)
{
   ID3D12Resource* res = getResource(resHnd);

   UINT8* dataBegin = 0;
   D3D12_RANGE readRange = {0, 0};
   DX12(res->Map(0, &readRange, reinterpret_cast<void**>(&dataBegin)));
   memcpy(dataBegin + offset, data, numBytes);
   res->Unmap(0, &D3D12_RANGE{offset, offset+numBytes});
}

void
gpuSetResourceData(ResourceHandle resHnd, const void* data, size_t numBytes)
{
   ID3D12Resource* res = getResource(resHnd);

   UINT8* dataBegin = 0;
   D3D12_RANGE readRange = {0, 0};
   DX12(res->Map(0, &readRange, reinterpret_cast<void**>(&dataBegin)));
   memcpy(dataBegin, data, numBytes);
   res->Unmap(0, &D3D12_RANGE{0, numBytes});
}

ResourceHandle
gpuCreateResource(size_t numBytes, char* debugName, GPUHeapType heapType, ResourceState state, UINT alignment, CreateResourceFlags flags)
{
   Renderer* r = gRenderCore;

   D3D12_RESOURCE_FLAGS resFlags = D3D12_RESOURCE_FLAG_NONE;

   if (flags & CreateResourceFlags_AllowUnordered) {
      resFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
   }

   D3D12_RESOURCE_DESC desc = resourceDescForBuffer(numBytes, alignment, resFlags);

   ID3D12Resource* resource = {};
   DX12(
      r->device->CreateCommittedResource(
           &heapProperties(toDXHeapType[heapType]),
           D3D12_HEAP_FLAG_NONE,
           &desc,
           // D3D12_RESOURCE_STATE_GENERIC_READ,
           toDXState[state],
           nullptr,
           IID_PPV_ARGS(&resource))
   );
   Assert(resource);
   if (debugName) {
      sz len = strlen(debugName);
      wchar_t* wideName = AllocateArray(wchar_t, len + 1, Lifetime_App);
      for (sz i = 0; i < len; ++i) {
         wideName[i] = (wchar_t)debugName[i];
      }

      resource->SetName(wideName);
   }
   // TODO: We probably want to specify lifetimes for these.
   ResourceHandle resHnd = gpuPushResource(resource);
   SBPush(r->sAppResources, resHnd, Lifetime_App);
   return resHnd;
}


Texture2D
gpuCreateTexture2D(int width, int height, bool withUAV, TextureFormat formatDesc, ResourceState stateDesc)
{
   Renderer* r = gRenderCore;

   Texture2D tex = {};

   DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

   if (formatDesc ==  TextureFormat::R32_UINT) {
      format = DXGI_FORMAT_R32_UINT;
   }

   D3D12_RESOURCE_FLAGS resFlags = D3D12_RESOURCE_FLAG_NONE;
   if (withUAV) {
      resFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
   }

   D3D12_RESOURCE_STATES resState = toDXState[stateDesc];

   ID3D12Resource* resource = {};
   r->device->CreateCommittedResource(
      &heapProperties(D3D12_HEAP_TYPE_DEFAULT),
      D3D12_HEAP_FLAG_NONE,
      &resourceDescForTexture2D(format, width, height, resFlags),
      resState,
      nullptr,
      IID_PPV_ARGS(&resource));
   resource->SetName(L"Texture");

   tex.resourceHandle = gpuPushResource(resource);

   D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
   srvDesc.Format = format;
   srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
   srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
   srvDesc.Texture2D.MostDetailedMip = 0;
   srvDesc.Texture2D.MipLevels = 1;
   srvDesc.Texture2D.PlaneSlice = 0;
   srvDesc.Texture2D.ResourceMinLODClamp = 0;

   tex.srvBindIndex = gpuAllocateSRVDescriptor(&srvDesc, getResource(tex.resourceHandle));
   if (withUAV) {
      D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
      uavDesc.Format = format;
      uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

      tex.uavBindIndex = gpuAllocateUAVDescriptor(&uavDesc, getResource(tex.resourceHandle));
   }

   return tex;
}

TextureCube
gpuCreateTextureCube(int width, int height, TextureFormat formatDesc, ResourceState stateDesc)
{
   Renderer* r = gRenderCore;
   TextureCube tex = {};

   tex.format = formatDesc;

   D3D12_RESOURCE_FLAGS resFlags = D3D12_RESOURCE_FLAG_NONE;

   DXGI_FORMAT format;
   if (formatDesc == TextureFormat::R32_UINT) {
      format = DXGI_FORMAT_R32_UINT;
   }
   else if (formatDesc == TextureFormat::R8_UNORM) {
      format = DXGI_FORMAT_R8_UNORM;
   }
   else if (formatDesc == TextureFormat::R8G8B8A8_UNORM) {
      format = DXGI_FORMAT_R8G8B8A8_UNORM;
   }
   else if (formatDesc == TextureFormat::D32_FLOAT) {
      format = DXGI_FORMAT_D32_FLOAT;
      resFlags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
   }
   else if (formatDesc == TextureFormat::R32_FLOAT) {
      format = DXGI_FORMAT_R32_FLOAT;
   }

   if (stateDesc == ResourceState_RenderTarget) {
      resFlags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;  // TODO: We probably want to set this without making it the initial state...
   }
   if (stateDesc == ResourceState_DepthWrite) {
      resFlags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;  // TODO: Same here.
   }

   D3D12_RESOURCE_STATES resState = toDXState[stateDesc];
   ID3D12Resource* resource = {};

   r->device->CreateCommittedResource(
      &heapProperties(D3D12_HEAP_TYPE_DEFAULT),
      D3D12_HEAP_FLAG_NONE,
      &resourceDescForTextureCube(format, width, height, resFlags),
      resState,
      nullptr,
      IID_PPV_ARGS(&resource));
   {
      static int nameIdx = 0;
      wchar_t name[128];

      swprintf(name, L"Cube texture %d", nameIdx++);
      resource->SetName(name);
   }

   tex.resourceHandle = gpuPushResource(resource);

   // Cube SRV view
   {
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
      srvDesc.Format = (format == DXGI_FORMAT_D32_FLOAT) ? DXGI_FORMAT_R32_FLOAT : format;
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srvDesc.TextureCube.MostDetailedMip = 0;
      srvDesc.TextureCube.MipLevels = 1;  // TODO: we're specifying mips in the resource description but only one in the SRV, for all texture types.
      srvDesc.TextureCube.ResourceMinLODClamp = 0;

      tex.srvCubeBindIndex = gpuAllocateSRVDescriptor(&srvDesc, getResource(tex.resourceHandle));
   }

   // Faces SRV views.
   for (int i = 0; i < 6; ++i) {
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
      srvDesc.Format = (format == DXGI_FORMAT_D32_FLOAT) ? DXGI_FORMAT_R32_FLOAT : format;
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
      srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srvDesc.Texture2DArray.MostDetailedMip = 0;
      srvDesc.Texture2DArray.MipLevels = 1; // TODO: we're specifying mips in the resource description but only one in the SRV, for all texture types.
      srvDesc.Texture2DArray.FirstArraySlice = i;
      srvDesc.Texture2DArray.ArraySize = 1;  // Just one
      srvDesc.Texture2DArray.PlaneSlice = 0;
      srvDesc.Texture2DArray.ResourceMinLODClamp = 0;

      tex.srvFaceBindIndex[i] = gpuAllocateSRVDescriptor(&srvDesc, getResource(tex.resourceHandle));
   }

   return tex;
}

D3D12_CPU_DESCRIPTOR_HANDLE
allocateRTVHandle()
{
   Renderer* r = gRenderCore;
   D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(r->rtvHeap->GetCPUDescriptorHandleForHeapStart());

   if (arrlen(r->sFreeRTVHandles)) {
      rtvHandle = arrpop(r->sFreeRTVHandles);
   }
   else {
      UINT rtvDescriptorSize = r->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

      rtvHandle.ptr += (UINT64)rtvDescriptorSize * r->rtvDescCount++;

      rtvHandle = rtvHandle;
   }

   return rtvHandle;
}

RenderTarget*
gpuCreateColorTarget(int width, int height, ResourceState state, RTFlags flags)
{
   Renderer* r = gRenderCore;


   RenderTarget* rt = AllocateElem(RenderTarget, Lifetime_App);

   ID3D12Resource* outRes = {};

   DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

   D3D12_RESOURCE_FLAGS resFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
   if ((int)flags & RTFlags_WithUAV) {
      resFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
   }

   D3D12_RESOURCE_DESC desc =
      (flags & RTFlags_Multisampled) ?
         resourceDescForTexture2DMS(
           format,
           width,
           height,
           gKnobs.MSSampleCount,
           resFlags)
      :
         resourceDescForTexture2D(
           format,
           width,
           height,
           resFlags);

   r->device->CreateCommittedResource(
       &heapProperties(D3D12_HEAP_TYPE_DEFAULT),
       D3D12_HEAP_FLAG_NONE,
       &desc,
       toDXState[state],
       nullptr /*clear value*/,
       IID_PPV_ARGS(&outRes));
   outRes->SetName(L"Color target");

   rt->resourceHandle = gpuPushResource(outRes);

   D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = allocateRTVHandle();

   r->device->CreateRenderTargetView(outRes, nullptr, rtvHandle);

   rt->descriptorHandle = rtvHandle;

   if (flags & RTFlags_WithSRV) {
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
      srvDesc.Format = format;
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srvDesc.Texture2D.MostDetailedMip = 0;
      srvDesc.Texture2D.MipLevels = 1;
      srvDesc.Texture2D.PlaneSlice = 0;
      srvDesc.Texture2D.ResourceMinLODClamp = 0;

      rt->bindIndexSRV = gpuAllocateSRVDescriptor(&srvDesc, outRes);
   }

   if (flags & RTFlags_WithUAV) {
      D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
      uavDesc.Format = format;
      uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

      rt->bindIndexUAV = gpuAllocateUAVDescriptor(&uavDesc, outRes);
   }

   rt->creationFlags = flags;

   return rt;
}

D3D12_CPU_DESCRIPTOR_HANDLE
allocateDSVHandle()
{
   Renderer* r = gRenderCore;

   D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle(r->dsvHeap->GetCPUDescriptorHandleForHeapStart());

   if (arrlen(r->sFreeDSVHandles)) {
      dsvHandle = arrpop(r->sFreeDSVHandles);
   }
   else {
      UINT dsvDescriptorSize = r->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

      dsvHandle.ptr += (UINT64)dsvDescriptorSize * r->dsvDescCount++;
   }

   return dsvHandle;
}

RenderTarget*
gpuCreateDepthTargetForCubeFace(TextureCube* cube, int width, int height, CubeFace face)
{
   Renderer* r = gRenderCore;

   D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = allocateDSVHandle();

   RenderTarget* rt = AllocateElem(RenderTarget, Lifetime_App);

   rt->creationFlags = RTFlags_WithSRV;

   rt->descriptorHandle = dsvHandle;

   ID3D12Resource* res = getResource(cube->resourceHandle);

   D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
   dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
   dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
   dsvDesc.Texture2DArray.MipSlice = 0;
   dsvDesc.Texture2DArray.FirstArraySlice = face;
   dsvDesc.Texture2DArray.ArraySize = 1;

   r->device->CreateDepthStencilView(res, &dsvDesc, dsvHandle);

   rt->resourceHandle = cube->resourceHandle;
   rt->bindIndexSRV = cube->srvFaceBindIndex[face];

   return rt;
}

RenderTarget*
gpuCreateColorTargetForCubeFace(TextureCube* cube, int width, int height, CubeFace face)
{
   Renderer* r = gRenderCore;

   D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = allocateRTVHandle();

   D3D12_RESOURCE_FLAGS resFlags = D3D12_RESOURCE_FLAG_NONE;

   RenderTarget* rt = AllocateElem(RenderTarget, Lifetime_App);

   rt->creationFlags = RTFlags_WithSRV;

   rt->descriptorHandle = rtvHandle;

   ID3D12Resource* res = getResource(cube->resourceHandle);

   DXGI_FORMAT format;

   if (cube->format == TextureFormat::R32_UINT) {
      format = DXGI_FORMAT_R32_UINT;
   }
   else if (cube->format == TextureFormat::R8G8B8A8_UNORM) {
      format = DXGI_FORMAT_R8G8B8A8_UNORM;
   }
   else if (cube->format == TextureFormat::R8_UNORM) {
      format = DXGI_FORMAT_R8_UNORM;
   }
   else if (cube->format == TextureFormat::D32_FLOAT) {
      format = DXGI_FORMAT_D32_FLOAT;
      resFlags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
   }
   else if (cube->format == TextureFormat::R32_FLOAT) {
      format = DXGI_FORMAT_R32_FLOAT;
   }

   D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
   rtvDesc.Format = format;
   rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
   rtvDesc.Texture2DArray.MipSlice = 0;
   rtvDesc.Texture2DArray.FirstArraySlice = face;
   rtvDesc.Texture2DArray.ArraySize = 1;

   r->device->CreateRenderTargetView(res, &rtvDesc, rtvHandle);

   rt->resourceHandle = cube->resourceHandle;
   rt->bindIndexSRV = cube->srvFaceBindIndex[face];

   return rt;
}

RenderTarget*
gpuCreateDepthTarget(int width, int height, RTFlags flags)
{
   Renderer* r = gRenderCore;

   D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = allocateDSVHandle();

   RenderTarget* rt = AllocateElem(RenderTarget, Lifetime_App);

   rt->creationFlags = flags;

   rt->descriptorHandle = dsvHandle;

   D3D12_CLEAR_VALUE depthClearValue = {};
   depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;
   depthClearValue.DepthStencil.Depth = 1.0f;
   depthClearValue.DepthStencil.Stencil = 0;

   D3D12_RESOURCE_DESC desc;
   if (flags & RTFlags_Multisampled) {
      desc = resourceDescForTexture2DMS(DXGI_FORMAT_D32_FLOAT, width, height, gKnobs.MSSampleCount, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
   }
   else {
      desc = resourceDescForTexture2D(DXGI_FORMAT_D32_FLOAT, width, height, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
   }

   ID3D12Resource* res = {};
   r->device->CreateCommittedResource(
       &heapProperties(D3D12_HEAP_TYPE_DEFAULT),
       D3D12_HEAP_FLAG_NONE,
       &desc,
       D3D12_RESOURCE_STATE_DEPTH_WRITE,
       &depthClearValue,
       IID_PPV_ARGS(&res));

   rt->resourceHandle = gpuPushResource(res);
   res->SetName(L"Depth/Stencil Resource Heap");

   r->device->CreateDepthStencilView(res, nullptr, dsvHandle);

   if (flags & RTFlags_WithSRV) {
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
      srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
      srvDesc.ViewDimension = gKnobs.useMultisampling ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
      srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srvDesc.Texture2D.MostDetailedMip = 0;
      srvDesc.Texture2D.MipLevels = 1;
      srvDesc.Texture2D.PlaneSlice = 0;
      srvDesc.Texture2D.ResourceMinLODClamp = 0;

      rt->bindIndexSRV = gpuAllocateSRVDescriptor(&srvDesc, res);
   }

   return rt;
}

PipelineStateHandle
gpuCreateRaytracingPipeline(RaytracingPSODesc desc)
{
   Renderer* r = gRenderCore;

   PipelineStateHandle pso = {};
   pso.type = PipelineStateHandle_Raytracing;

   RaytracingPSO res;

   res.numHitGroups = SBCount(desc.sHitGroups);
   res.numMissShaders = SBCount(desc.sMissIds);

   // Max subobjects
   // 1 - DXIL lib
   // 1 - pipeline config
   // 1 - Payload shader config
   // 1 - Local root sig
   // n - Hitgroups
   // ---
   // 4 + n

   auto* subobjects = AllocateArray(D3D12_STATE_SUBOBJECT, 4 + res.numHitGroups, Lifetime_Frame);
   int numSubobjects = 0;

   // Add state subobject for shader code
   {
      auto* libDesc = AllocateElem(D3D12_DXIL_LIBRARY_DESC, Lifetime_Frame);
      libDesc->DXILLibrary.BytecodeLength = desc.szByteCode;
      libDesc->DXILLibrary.pShaderBytecode = desc.byteCode;
      // libDesc->NumExports = 1;
      // libDesc->pExports = exportDesc;

      D3D12_STATE_SUBOBJECT lib = {};
      lib.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
      lib.pDesc = libDesc;

      subobjects[numSubobjects++] = lib;
   }

   // Pipeline config
   D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
   pipelineConfig.MaxTraceRecursionDepth = 2;

   D3D12_STATE_SUBOBJECT pipelineConfigObject = {};
   pipelineConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
   pipelineConfigObject.pDesc = &pipelineConfig;

   subobjects[numSubobjects++] = pipelineConfigObject;

   // Hit groups
   for (int hgIdx = 0; hgIdx < SBCount(desc.sHitGroups); ++hgIdx) {
      RaytracingHitgroup* hg = desc.sHitGroups + hgIdx;

      auto* hitGroupDesc = AllocateElem(D3D12_HIT_GROUP_DESC, Lifetime_Frame);
      hitGroupDesc->Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
      hitGroupDesc->ClosestHitShaderImport = hg->closestHit;
      hitGroupDesc->AnyHitShaderImport = hg->anyHit;
      hitGroupDesc->IntersectionShaderImport = hg->intersection;

      hitGroupDesc->HitGroupExport = hg->name;

      D3D12_STATE_SUBOBJECT hitGroup = {};
      hitGroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
      hitGroup.pDesc = hitGroupDesc;

      subobjects[numSubobjects++] = hitGroup;
   }

   // Shader payload
   D3D12_RAYTRACING_SHADER_CONFIG shaderDesc = {};
   shaderDesc.MaxPayloadSizeInBytes = sizeof(float);
   shaderDesc.MaxAttributeSizeInBytes = D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;

   D3D12_STATE_SUBOBJECT shaderConfig = {};
   shaderConfig.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
   shaderConfig.pDesc = &shaderDesc;

   subobjects[numSubobjects++] = shaderConfig;

   // Local root signature

   D3D12_LOCAL_ROOT_SIGNATURE localRootSig = {};
   localRootSig.pLocalRootSignature = r->dxrRootSignature;

   D3D12_STATE_SUBOBJECT localRootSigObj;
   localRootSigObj.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
   localRootSigObj.pDesc = &localRootSig;

   subobjects[numSubobjects++] = localRootSigObj;


   // TODO: Why does this crash CreateStateObject?
   // Global root signature

   // D3D12_STATE_SUBOBJECT globalRootSig;
   // globalRootSig.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
   // globalRootSig.pDesc = r->dxrRootSignature;

   // subobjects[numSubobjects++] = globalRootSig;

   D3D12_STATE_OBJECT_DESC od = {};
   od.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

   od.NumSubobjects = numSubobjects;
   od.pSubobjects = subobjects;

   if (HRESULT hr = r->dxrDevice->CreateStateObject(&od, IID_PPV_ARGS(&res.obj)) != S_OK) {
      handleDx12Error("failed to create state object");
   }

   res.obj->SetName(L"DXR Pipeline");

   u64 shaderRecordSize = AlignPow2(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + 3*8 /* descriptor table pointers*/,
                                    D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

   wchar_t** sShaderIds = {};
   SBPush(sShaderIds, L"RayGen", Lifetime_Frame);

   for (int i = 0; i < SBCount(desc.sMissIds); ++i) {
      SBPush(sShaderIds, desc.sMissIds[i], Lifetime_Frame);
   }
   for (int i = 0; i < SBCount(desc.sHitGroups); ++i) {
      SBPush(sShaderIds, desc.sHitGroups[i].name, Lifetime_Frame);
   }

   //wchar_t* shaderIds[] = { L"RayGen",  L"Miss", L"HitGroup", };

   u64 tableSize = AlignPow2(SBCount(sShaderIds) * shaderRecordSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

   ResourceHandle shaderTableHnd = gpuCreateResource(tableSize, "Shader table");

   ID3D12Resource* shaderTable = getResource(shaderTableHnd);

   // Copy shader identifiers from pso info to the shader table. Dear christ make it stop.

   ID3D12StateObjectProperties* psoInfo ={};
   DX12(res.obj->QueryInterface(IID_PPV_ARGS(&psoInfo)));

   uint8_t* out = {};
   shaderTable->Map(0, nullptr, (void**)&out);
   for (int exportIdx = 0; exportIdx < SBCount(sShaderIds); ++exportIdx) {
      memcpy(out, psoInfo->GetShaderIdentifier(sShaderIds[exportIdx]), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
      // Set the root parameter data. Point to start of descriptor heap.
      *reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(out + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = r->grvHeap->GetGPUDescriptorHandleForHeapStart();
      *reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(out + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + 8) = r->grvHeap->GetGPUDescriptorHandleForHeapStart();
      *reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(out + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + 16) = r->grvHeap->GetGPUDescriptorHandleForHeapStart();
      out += shaderRecordSize;
   }

   // Unmap
   shaderTable->Unmap(0, nullptr);

   res.shaderTable = shaderTable;
   res.recordSize = shaderRecordSize;
   res.recordTableSize = tableSize;

   pso.idx = SBCount(r->sDxrPSOs);
   SBPush(r->sDxrPSOs, res, Lifetime_App);

   return pso;
}

void gpuFreeMarkedResources();


void
gpuResize(int width, int height)
{
   Renderer*r = gRenderCore;

   // Free RT resources.
   for (int i = 0; i < gKnobs.swapChainBufferCount; ++i) {
      // We can't just call gpuMarkFreeRenderTarget on the resolve target because we got it directly from the swap chain.
      gpuMarkFreeResource(r->rtResolve[i]->resourceHandle, r->frameCount);

      gpuMarkFreeDepthTarget(r->rtDepth[i], r->frameCount);
   }

   // Signal and wait.
   gpuFlush();

   gpuFreeMarkedResources();


   if (FAILED(r->swapChain->ResizeBuffers(gKnobs.swapChainBufferCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, NULL))) {
      logMsg("Failed to resize buffer!");
   }


   #if 1
   UINT rtvDescriptorSize = r->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

   // Create a resolve target for each frame.
   for (UINT n = 0; n < gKnobs.swapChainBufferCount; n++) {
      // Resolve
      {
         D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = allocateRTVHandle();

         DX12(r->swapChain->GetBuffer(n, IID_PPV_ARGS(&r->backBufferRes[n])));
         r->device->CreateRenderTargetView(r->backBufferRes[n], nullptr, rtvHandle);
         r->rtResolve[n]->descriptorHandle = rtvHandle;
         r->rtResolve[n]->resourceHandle = gpuPushResource(r->backBufferRes[n]);
         rtvHandle.ptr += (UINT64)rtvDescriptorSize;
      }

      // Depth
      r->rtDepth[n] = gpuCreateDepthTarget(width, height, gKnobs.useMultisampling ? RTFlags_Multisampled : RTFlags_None);
   }
   r->fbWidth = width;
   r->fbHeight = height;
   r->frameIdxModulo = r->swapChain->GetCurrentBackBufferIndex();
   #endif
}

void
gpuGoFullscreen()
{
   Renderer* r = gRenderCore;
   if (FAILED(r->swapChain->SetFullscreenState(true, nullptr))) {
      logMsg("Failed to go full-screen!");
   }
}

void
gpuInit(Platform* plat, int width, int height)
{
   Assert(ArrayCount(toDXState) == ResourceState_Count);

   const HWND hwnd = (HWND)plat->getWindowHandle();

   gpuPixInit(plat);

   plat->getClientRect(&width, &height);

   Renderer* r = gRenderCore;

   r->plat = plat;

   // Construct vtables for things inside Renderer..
   #if BuildMode(EngineDebug)
   {
      new(r)Renderer;
   }
   #endif

   // Sentinels
   {
      gpuPushResource(nullptr);
      SBPush(r->sBLAS, BLAS{}, Lifetime_App);
      SBPush(r->sDxrPSOs, RaytracingPSO{}, Lifetime_App);
      SBPush(r->sPSOs, nullptr, Lifetime_App);
   }

   ID3D12Debug* debugController = {};
   IDXGIFactory4* factory = {};
   IDXGIAdapter1* hwAdapter = {};

   r->frameCount = 1;  // Set first frame to 1 so that GetCompletedValue() is meaningful

   r->fbWidth = width;
   r->fbHeight = height;

   UINT dxgiFactoryFlags = 0;

   if (gKnobs.gpuDebug && SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
   {
      debugController->EnableDebugLayer();

      // Enable additional debug layers.
      dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
   }

   DX12(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

   // Grab HW adapter
   {
      for (UINT adapterIdx = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(adapterIdx, &hwAdapter); ++adapterIdx) {
         DXGI_ADAPTER_DESC1 desc;
         hwAdapter->GetDesc1(&desc);

         if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            // Don't select the Basic Render Driver adapter.
            // If you want a software adapter, pass in "/warp" on the command line.
            continue;
         }

         // Check to see if the adapter supports Direct3D 12, but don't create the
         // actual device yet.
         if (SUCCEEDED(D3D12CreateDevice(hwAdapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) {
            break;
         }
      }
   }

   DX12( D3D12CreateDevice(
     hwAdapter,
     D3D_FEATURE_LEVEL_11_0,
     IID_PPV_ARGS(&r->device)));

   if (gKnobs.withRtx) {
      r->device->QueryInterface(IID_PPV_ARGS(&r->dxrDevice));
      if (!r->dxrDevice)  {
         OutputDebugStringA("Could not get dxr interface");
         gKnobs.withRtx = false;
      }
      else {
         r->device = r->dxrDevice;
      }
   }

   #if 0
   ID3D12InfoQueue* pInfoQueue = {};
   r->device->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
   if (pInfoQueue) {
      dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
      D3D12_MESSAGE_ID DenyIds[] = {
         D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
      };

      D3D12_INFO_QUEUE_FILTER NewFilter = {};
      NewFilter.DenyList.NumIDs = _countof(DenyIds);
      NewFilter.DenyList.pIDList = DenyIds;

      DX12(pInfoQueue->PushStorageFilter(&NewFilter));
   }
   #endif

   {
      D3D12_COMMAND_QUEUE_DESC queueDesc = {};
      queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
      queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

      DX12(r->device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&r->commandQueue)));
      r->commandQueue->SetName(L"Default command queue");
   }

   {
      DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
      swapChainDesc.BufferCount = gKnobs.swapChainBufferCount;
      swapChainDesc.Width = width;
      swapChainDesc.Height = height;
      swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
      swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
      swapChainDesc.SampleDesc.Count = 1;

      IDXGISwapChain1* swapChain1;
      DX12(factory->CreateSwapChainForHwnd(
               r->commandQueue,
               hwnd,
               &swapChainDesc,
               nullptr,
               nullptr,
               &swapChain1));
      r->swapChain = static_cast<IDXGISwapChain3*>(swapChain1);
   }

   r->frameIdxModulo = r->swapChain->GetCurrentBackBufferIndex();

   // Create render targets
   {
      D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
      rtvHeapDesc.NumDescriptors = gKnobs.numRTVDescriptors;
      rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
      rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
      DX12(r->device->CreateDescriptorHeap(
         &rtvHeapDesc, IID_PPV_ARGS(&r->rtvHeap)));

      D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
      dsvHeapDesc.NumDescriptors = gKnobs.numDSVDescriptors;
      dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
      dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
      DX12(r->device->CreateDescriptorHeap(
         &dsvHeapDesc, IID_PPV_ARGS(&r->dsvHeap)));

      UINT rtvDescriptorSize = r->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

      // Create a resolve target for each frame.
      for (UINT n = 0; n < gKnobs.swapChainBufferCount; n++) {
         // Resolve
         {
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(r->rtvHeap->GetCPUDescriptorHandleForHeapStart());
            rtvHandle.ptr += (UINT64)rtvDescriptorSize * r->rtvDescCount++;

            r->rtResolve[n] = AllocateElem(RenderTarget, Lifetime_App);
            // Resolve
            DX12(r->swapChain->GetBuffer(n, IID_PPV_ARGS(&r->backBufferRes[n])));
            r->device->CreateRenderTargetView(r->backBufferRes[n], nullptr, rtvHandle);
            r->rtResolve[n]->descriptorHandle = rtvHandle;
            r->rtResolve[n]->resourceHandle = gpuPushResource(r->backBufferRes[n]);
            rtvHandle.ptr += (UINT64)rtvDescriptorSize;
         }
         // Color
         if (gKnobs.useMultisampling) {
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(r->rtvHeap->GetCPUDescriptorHandleForHeapStart());
            rtvHandle.ptr += (UINT64)rtvDescriptorSize * r->rtvDescCount++;

            // r->rtColor[n] = AllocateElem(RenderTarget, Lifetime_App);
            // D3D12_CLEAR_VALUE clearValue;
            // clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            // clearValue.Color[0] = clearValue.Color[1] = clearValue.Color[2] = 0.0f;
            // clearValue.Color[3] = 1.0f;
            // ID3D12Resource* res = {};
            // r->device->CreateCommittedResource(
            //    &heapProperties(D3D12_HEAP_TYPE_DEFAULT),
            //    D3D12_HEAP_FLAG_NONE,
            //    &resourceDescForTexture2DMS(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, gKnobs.MSSampleCount, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
            //    D3D12_RESOURCE_STATE_RESOLVE_SOURCE,  // Transitions to RENDER_TARGET at the start of the frame
            //    nullptr,//&clearValue,
            //    IID_PPV_ARGS(&res));
            // res->SetName(L"Color buffer");
            // r->device->CreateRenderTargetView(res, nullptr, rtvHandle);
            // r->rtColor[n]->resourceHandle = gpuPushResource(res);


            // r->rtColor[n]->descriptorHandle = rtvHandle;
         }

         // Depth
         r->rtDepth[n] = gpuCreateDepthTarget(width, height, gKnobs.useMultisampling ? RTFlags_Multisampled : RTFlags_None);
      }
   }

   // SRV, UAV descriptor heaps
   {
      D3D12_DESCRIPTOR_HEAP_DESC grvHeapDesc = {};
      grvHeapDesc.NumDescriptors = gKnobs.numSRVDescriptors + gKnobs.numUAVDescriptors + gKnobs.numCBVDescriptors;
      grvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      grvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
      DX12(r->device->CreateDescriptorHeap(
         &grvHeapDesc, IID_PPV_ARGS(&r->grvHeap)));

      // Create NULL descriptors.
      int grvDescSize = r->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

      for (int i = 0; i < gKnobs.numSRVDescriptors; ++i) {
         D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
         srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
         srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
         srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
         srvDesc.Texture2D.MostDetailedMip = 0;
         srvDesc.Texture2D.MipLevels = 1;
         srvDesc.Texture2D.PlaneSlice = 0;
         srvDesc.Texture2D.ResourceMinLODClamp = 0;

         D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = r->grvHeap->GetCPUDescriptorHandleForHeapStart();
         srvHandle.ptr += i * grvDescSize;
         r->device->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);
      }

      for (int i = 0; i < gKnobs.numUAVDescriptors; ++i) {
         D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
         uavDesc.Format = DXGI_FORMAT_R8_UNORM;
         uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;

         D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = r->grvHeap->GetCPUDescriptorHandleForHeapStart();
         uavHandle.ptr += (i + gKnobs.numSRVDescriptors) * grvDescSize;
         r->device->CreateUnorderedAccessView(nullptr, nullptr, &uavDesc, uavHandle);
      }

      for (u64 i = 0; i < gKnobs.numCBVDescriptors; ++i) {
         gpuAllocateCBVDescriptor(NULL);
      }

   }

   for (sz i = 0; i < gKnobs.swapChainBufferCount; ++i) {
      DX12(r->device->CreateCommandAllocator(
         D3D12_COMMAND_LIST_TYPE_DIRECT,
         IID_PPV_ARGS(&r->commandAllocator[i])));
   }

   // Upload heap
   {
      for (int i = 0; i < gKnobs.swapChainBufferCount; ++i) {
         D3D12_HEAP_PROPERTIES uploadHeapProperties = {};

         uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
         uploadHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
         uploadHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
         uploadHeapProperties.VisibleNodeMask = 1;
         uploadHeapProperties.CreationNodeMask = 1;

         ResourceHandle h = gpuCreateResource(gKnobs.uploadHeapSizeBytes, "Upload heap");
         r->uploadHeaps[i] = getResource(h);
      }
      r->uploadHeapUsedBytes = 0;
   }

   // Create frame fence
   r->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&r->frameFence));

   r->frameFenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

   // Resources shared by graphics and compute root signatures.

   D3D12_STATIC_SAMPLER_DESC pointSampler = {};
   {
      pointSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
      pointSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      pointSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      pointSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      pointSampler.MipLODBias = 0.f;
      pointSampler.MaxAnisotropy = 0;
      pointSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
      pointSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
      pointSampler.MinLOD = 0.f;
      pointSampler.MaxLOD = 0.f;
      pointSampler.ShaderRegister = 0;
      pointSampler.RegisterSpace = 0;
      pointSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
   }
   D3D12_STATIC_SAMPLER_DESC linearSampler = {};
   {
      linearSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
      linearSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      linearSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      linearSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      linearSampler.MipLODBias = 0.f;
      linearSampler.MaxAnisotropy = 0;
      linearSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
      linearSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
      linearSampler.MinLOD = 0.f;
      linearSampler.MaxLOD = 0.f;
      linearSampler.ShaderRegister = 1;
      linearSampler.RegisterSpace = 0;
      linearSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
   }

   D3D12_ROOT_PARAMETER srvRangeParameterTex2D = {};
   D3D12_ROOT_DESCRIPTOR_TABLE srvDescriptorTable2D = {};
   D3D12_DESCRIPTOR_RANGE descRangeSRV2D = {};
   {
      descRangeSRV2D.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

      descRangeSRV2D.NumDescriptors = gKnobs.numSRVDescriptors;
      descRangeSRV2D.BaseShaderRegister = 0;
      descRangeSRV2D.RegisterSpace = 0;
      descRangeSRV2D.OffsetInDescriptorsFromTableStart = 0;

      srvDescriptorTable2D.NumDescriptorRanges = 1;
      srvDescriptorTable2D.pDescriptorRanges = &descRangeSRV2D;

      srvRangeParameterTex2D.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      srvRangeParameterTex2D.DescriptorTable = srvDescriptorTable2D;
      srvRangeParameterTex2D.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
   }

   D3D12_ROOT_PARAMETER srvRangeParameterTex3D = {};
   D3D12_ROOT_DESCRIPTOR_TABLE srvDescriptorTable3D = {};
   D3D12_DESCRIPTOR_RANGE descRangeSRV3D = {};
   {
      descRangeSRV3D.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

      descRangeSRV3D.NumDescriptors = gKnobs.numSRVDescriptors;
      descRangeSRV3D.BaseShaderRegister = 0;
      descRangeSRV3D.RegisterSpace = 1;
      descRangeSRV3D.OffsetInDescriptorsFromTableStart = 0;

      srvDescriptorTable3D.NumDescriptorRanges = 1;
      srvDescriptorTable3D.pDescriptorRanges = &descRangeSRV3D;
      srvRangeParameterTex3D.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      srvRangeParameterTex3D.DescriptorTable = srvDescriptorTable3D;
      srvRangeParameterTex3D.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
   }

   D3D12_ROOT_PARAMETER srvRangeParameterTexCube = {};
   D3D12_ROOT_DESCRIPTOR_TABLE srvDescriptorTableCube = {};
   D3D12_DESCRIPTOR_RANGE descRangeSRVCube = {};
   {
      descRangeSRVCube.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

      descRangeSRVCube.NumDescriptors = gKnobs.numSRVDescriptors;
      descRangeSRVCube.BaseShaderRegister = 0;
      descRangeSRVCube.RegisterSpace = 2;
      descRangeSRVCube.OffsetInDescriptorsFromTableStart = 0;

      srvDescriptorTableCube.NumDescriptorRanges = 1;
      srvDescriptorTableCube.pDescriptorRanges = &descRangeSRVCube;
      srvRangeParameterTexCube.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      srvRangeParameterTexCube.DescriptorTable = srvDescriptorTableCube;
      srvRangeParameterTexCube.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
   }

   D3D12_ROOT_PARAMETER uavRangeParameter = {};
   D3D12_ROOT_DESCRIPTOR_TABLE uavDescriptorTable = {};
   D3D12_DESCRIPTOR_RANGE descRangeUAV = {};
   {
      descRangeUAV.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

      descRangeUAV.NumDescriptors = gKnobs.numUAVDescriptors;
      descRangeUAV.BaseShaderRegister = 0;
      descRangeUAV.RegisterSpace = 0;
      descRangeUAV.OffsetInDescriptorsFromTableStart = gKnobs.numSRVDescriptors;

      uavDescriptorTable.NumDescriptorRanges = 1;
      uavDescriptorTable.pDescriptorRanges = &descRangeUAV;
      uavRangeParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      uavRangeParameter.DescriptorTable = uavDescriptorTable;
      uavRangeParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
   }

   D3D12_ROOT_PARAMETER cbvRootParameters[3] = {};
   for (int i = 0; i < ArrayCount(cbvRootParameters); ++i) {
      D3D12_ROOT_DESCRIPTOR cbvDesc = {};
      cbvDesc.ShaderRegister = i;
      cbvDesc.RegisterSpace = 0;

      cbvRootParameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      cbvRootParameters[i].Descriptor = cbvDesc;
      cbvRootParameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
   }

   // Make graphics and compute share the same root descriptor parameters, for simplicity...
   D3D12_ROOT_PARAMETER params[] = { cbvRootParameters[0], cbvRootParameters[1], cbvRootParameters[2], srvRangeParameterTex2D, srvRangeParameterTex3D, srvRangeParameterTexCube, uavRangeParameter };

   // Graphics root signature
   {
      // CBV parameter

      D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};

      rootSignatureDesc.NumParameters = ArrayCount(params);
      rootSignatureDesc.pParameters = params;

      D3D12_STATIC_SAMPLER_DESC samplers[] = { pointSampler, linearSampler };
      rootSignatureDesc.NumStaticSamplers = ArrayCount(samplers);
      rootSignatureDesc.pStaticSamplers = samplers;
      rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

      ID3DBlob* signature = {};
      ID3DBlob* error = {};
      {
         if (S_OK != D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error)) {
            // ??
            char* ptr = (char*)error->GetBufferPointer();
            handleDx12Error(ptr);
         }
         DX12(r->device->CreateRootSignature(
                  0,
                  signature->GetBufferPointer(),
                  signature->GetBufferSize(),
                  IID_PPV_ARGS(&r->graphicsRootSignature)));
         signature->Release();
      }
   }

   // Compute root signature
   {
      D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};

      rootSignatureDesc.NumParameters = ArrayCount(params);
      rootSignatureDesc.pParameters = params;

      D3D12_STATIC_SAMPLER_DESC samplers[] = { pointSampler };
      rootSignatureDesc.NumStaticSamplers = ArrayCount(samplers);
      rootSignatureDesc.pStaticSamplers = samplers;
      // rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

      ID3DBlob* signature = {};
      ID3DBlob* error = {};
      if (S_OK != D3D12SerializeRootSignature(
                     &rootSignatureDesc,
                     D3D_ROOT_SIGNATURE_VERSION_1,
                     &signature,
                     &error)) {
         char* ptr = (char*)error->GetBufferPointer();
         handleDx12Error(ptr);
      }

      DX12( r->device->CreateRootSignature(
               0,
               signature->GetBufferPointer(),
               signature->GetBufferSize(),
               IID_PPV_ARGS(&r->computeRootSignature)));
      signature->Release();
   }


   // Raytracing
   if (gKnobs.withRtx) {

      // For DXR we use descriptor ranges for CBV

      D3D12_ROOT_PARAMETER cbvRangeParameter = {};
      D3D12_ROOT_DESCRIPTOR_TABLE cbvDescriptorTable = {};
      D3D12_DESCRIPTOR_RANGE descRangeCBV = {};
      {
         descRangeCBV.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;

         descRangeCBV.NumDescriptors = gKnobs.numCBVDescriptors;
         descRangeCBV.BaseShaderRegister = 0;
         descRangeCBV.RegisterSpace = 0;
         descRangeCBV.OffsetInDescriptorsFromTableStart = gKnobs.numSRVDescriptors + gKnobs.numUAVDescriptors;

         cbvDescriptorTable.NumDescriptorRanges = 1;
         cbvDescriptorTable.pDescriptorRanges = &descRangeCBV;
         cbvRangeParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
         cbvRangeParameter.DescriptorTable = cbvDescriptorTable;
         cbvRangeParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      }

      D3D12_ROOT_PARAMETER rtparams[] = { srvRangeParameterTex2D, uavRangeParameter, cbvRangeParameter };

      D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
      rootSignatureDesc.NumParameters = ArrayCount(rtparams);
      rootSignatureDesc.pParameters = rtparams;
      rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

      ID3DBlob* signature = {};
      ID3DBlob* error = {};
      if (S_OK != D3D12SerializeRootSignature(
                     &rootSignatureDesc,
                     D3D_ROOT_SIGNATURE_VERSION_1,
                     &signature,
                     &error)) {
         char* ptr = (char*)error->GetBufferPointer();
         handleDx12Error(ptr);
      }

      if (!SUCCEEDED(r->device->CreateRootSignature(
               0,
               signature->GetBufferPointer(),
               signature->GetBufferSize(),
               IID_PPV_ARGS(&r->dxrRootSignature)))) {
         gKnobs.withRtx = false;
      }
      else {
         // Point bvh to the first descriptor...

         // Allocating a null descriptor. This slot will be filled with the correct descriptor when building the TLAS
         D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
         srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
         srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
         srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
         srvDesc.Texture2D.MostDetailedMip = 0;
         srvDesc.Texture2D.MipLevels = 1;
         srvDesc.Texture2D.PlaneSlice = 0;
         srvDesc.Texture2D.ResourceMinLODClamp = 0;

         r->bvhDescriptorIdx = gpuAllocateSRVDescriptor(&srvDesc, nullptr);
         Assert (r->bvhDescriptorIdx == 0);
         // Allocate another one, so we have one per swap buffer.
         gpuAllocateSRVDescriptor(&srvDesc, nullptr);
      }
      signature->Release();
   }

   // Create command list
   DX12(r->device->CreateCommandList(
      /*nodeMask*/0,
      D3D12_COMMAND_LIST_TYPE_DIRECT,
      r->commandAllocator[r->frameIdxModulo],
      nullptr /*initial PSO*/,
      IID_PPV_ARGS(&r->commandList)));

   r->commandList->SetName(L"Default command list");

   // Map current upload heap.
   r->uploadHeapUsedBytes = 0;
   DX12( r->uploadHeaps[r->frameIdxModulo]->Map(0, &D3D12_RANGE{0, 0}, reinterpret_cast<void**>(&r->uploadHeapPtr)) );
}

void
gpuSetRaytracingPipeline(PipelineStateHandle h)
{
   Renderer* r = gRenderCore;

   Assert (h.type == PipelineStateHandle_Raytracing);
   r->dxrCurrentPipeline = h;
   r->commandList->SetPipelineState1(r->sDxrPSOs[h.idx].obj);
}

void
gpuDispatchRays()
{
   Renderer* r = gRenderCore;
   WorldRender& wr = *gWorldRender;

   D3D12_DISPATCH_RAYS_DESC rd = {};

   r->commandList->SetComputeRootSignature(r->dxrRootSignature);

   RaytracingPSO* pso = r->sDxrPSOs + r->dxrCurrentPipeline.idx;

   rd.RayGenerationShaderRecord.StartAddress = pso->shaderTable->GetGPUVirtualAddress();
   rd.RayGenerationShaderRecord.SizeInBytes = pso->recordSize;

   rd.MissShaderTable.StartAddress = pso->shaderTable->GetGPUVirtualAddress() + pso->recordSize;
   rd.MissShaderTable.SizeInBytes = pso->recordSize * pso->numMissShaders;
   rd.MissShaderTable.StrideInBytes = pso->recordSize;

   rd.HitGroupTable.StartAddress = pso->shaderTable->GetGPUVirtualAddress() + pso->recordSize * (1 + pso->numMissShaders);
   rd.HitGroupTable.SizeInBytes = pso->recordSize * pso->numHitGroups;
   rd.HitGroupTable.StrideInBytes = pso->recordSize;

   rd.Width = r->fbWidth;
   rd.Height = r->fbHeight;
   rd.Depth = 1;

   r->commandList->DispatchRays(&rd);
}

void
gpuSetRaytracingConstantSlot(u64 slot, ResourceHandle resource, u64 numBytes)
{
   Renderer* r = gRenderCore;

   D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
   cbvDesc.SizeInBytes = AlignPow2(numBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
   cbvDesc.BufferLocation = getResource(resource)->GetGPUVirtualAddress();
   r->device->CreateConstantBufferView(&cbvDesc, gpuGetCBVDescriptorCPU(slot));
}

sz
gpuReserveUploadBuffer(const sz size, const sz alignment)
{
   Renderer* r = gRenderCore;

   sz alignedOffset = AlignPow2((sz)r->uploadHeapUsedBytes, alignment);

   sz alignBytes = alignedOffset - r->uploadHeapUsedBytes;

   if (r->uploadHeapUsedBytes + size + alignBytes > gKnobs.uploadHeapSizeBytes) {
      Assert(false);  // Need more upload heap size, or a better allocation scheme.
   }

   r->uploadHeapUsedBytes += size + alignBytes;

   return alignedOffset;
}

void
gpuUploadBufferAtOffset(ResourceHandle destResource, const u8* data, const sz size, const sz offset)
{
   Renderer* r = gRenderCore;

   if (r->uploadHeapUsedBytes + size > gKnobs.uploadHeapSizeBytes) {
      Assert(false);  // Need more upload heap size, or a better allocation scheme.
   }
   memcpy(r->uploadHeapPtr + r->uploadHeapUsedBytes, data, size);
   r->commandList->CopyBufferRegion(getResource(destResource), /*dstOffset*/offset, r->uploadHeaps[r->frameIdxModulo], /*src offset*/r->uploadHeapUsedBytes, size);

   r->uploadHeapUsedBytes += size;
}

void
gpuUploadBuffer(ResourceHandle destResource, const u8* data, const sz size)
{
   gpuUploadBufferAtOffset(destResource, data, size, 0);
}

Texture2D
gpuUploadTexture2D(const int width, const int height, const int bytesPerPixel, const u8* data)
{
   Renderer* r = gRenderCore;

   Texture2D result = gpuCreateTexture2D(width, height, /*uav*/false);

   D3D12_SUBRESOURCE_FOOTPRINT footprint = {};

   DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

   footprint.Format = format;
   footprint.Width = width;
   footprint.Height = height;
   footprint.Depth = 1;
   footprint.RowPitch = AlignPow2(width * 4/*regardless of source, we're storing rgba*/, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

   sz offset = gpuReserveUploadBuffer(
      footprint.Height * footprint.RowPitch,
      D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

   D3D12_PLACED_SUBRESOURCE_FOOTPRINT placedTexture = {};
   placedTexture.Offset = offset;
   placedTexture.Footprint = footprint;

   // Copy texture row by row to upload heap
   u8* copyPointer = r->uploadHeapPtr + offset;
   sz sourceRowSize = width * bytesPerPixel;
   if (bytesPerPixel == 4) {
      for (int j = 0; j < height; ++j) {
         const u8* inRow = data + (j * sourceRowSize);
         memcpy(copyPointer + (j * footprint.RowPitch), inRow, sourceRowSize);
      }
   }
   else if (bytesPerPixel == 3) {
      for (int j = 0; j < height; ++j) {
         const u8* inRow = data + (j * sourceRowSize);
         u8* outRow = (copyPointer + (j * footprint.RowPitch));
         for (int i = 0; i < width; ++i) {
            outRow[i*4 + 0] = inRow[i*3 + 0];
            outRow[i*4 + 1] = inRow[i*3 + 1];
            outRow[i*4 + 2] = inRow[i*3 + 2];
            outRow[i*4 + 3] = 255;
         }
      }
   }
   else if (bytesPerPixel == 1) {
      for (int j = 0; j < height; ++j) {
         const u8* inRow = data + (j * sourceRowSize);
         u8* outRow = (copyPointer + (j * footprint.RowPitch));
         for (int i = 0; i < width; ++i) {
            outRow[i*4 + 0] = inRow[i];
            outRow[i*4 + 1] = inRow[i];
            outRow[i*4 + 2] = inRow[i];
            outRow[i*4 + 3] = inRow[i];
         }
      }
   }
   else {
      Assert(false);  // ???
   }

   // Copy to texture

   D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
   dstLoc.pResource = getResource(result.resourceHandle);
   dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
   dstLoc.SubresourceIndex = 0;

   D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
   srcLoc.pResource = r->uploadHeaps[r->frameIdxModulo];
   srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
   srcLoc.PlacedFootprint = D3D12_PLACED_SUBRESOURCE_FOOTPRINT { offset, footprint };

   r->commandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

   gpuBarrierForResource(result.resourceHandle, ResourceState_CopyDest, ResourceState_PixelShaderResource);

   return result;
}

Texture3D
gpuCreateTexture3D(const int width, const int height, const int depth)
{
   Renderer* r = gRenderCore;

   Texture3D result = {};

   result.mipCount = log2(Min(Min(width, height), depth));

   D3D12_SUBRESOURCE_FOOTPRINT footprint = {};

   DXGI_FORMAT format = DXGI_FORMAT_R8G8_UNORM;

   footprint.Format = format;
   footprint.Width = width;
   footprint.Height = height;
   footprint.Depth = depth;
   footprint.RowPitch = AlignPow2(width, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

   ID3D12Resource* resource = {};
   r->device->CreateCommittedResource(
      &heapProperties(D3D12_HEAP_TYPE_DEFAULT),
      D3D12_HEAP_FLAG_NONE,
      &resourceDescForTexture3D(format, width, height, depth, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      nullptr,
      IID_PPV_ARGS(&resource));
   resource->SetName(L"SDF Texture");

   result.resourceHandle = gpuPushResource(resource);
   SBPush(r->sAppResources, result.resourceHandle, Lifetime_App);

   D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
   srvDesc.Format = format;
   srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
   srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
   srvDesc.Texture3D.MipLevels = result.mipCount;
   srvDesc.Texture3D.MostDetailedMip = 0;
   srvDesc.Texture3D.ResourceMinLODClamp = 0;

   result.bindIndexSRV = gpuAllocateSRVDescriptor(&srvDesc, resource);

   for (int i = 0; i < result.mipCount; ++i) {
      D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
      uavDesc.Format = format;
      uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
      uavDesc.Texture3D.MipSlice = i;
      uavDesc.Texture3D.WSize = static_cast<UINT>(-1);

      result.bindIndexUAV[i] = gpuAllocateUAVDescriptor(&uavDesc, resource);
   }

   return result;
}

PipelineStateHandle
gpuGraphicsPipelineState(u8* vertexByteCode, u64 szVertexByteCode, u8* pixelByteCode, u64 szPixelByteCode, const PSOFlags psoFlags)
{
   Renderer* r = gRenderCore;

   pushApiLifetime(Lifetime_App);

   ID3D12PipelineState* pso = {};

   D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
   {
       { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, OffsetOf(MeshRenderVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
       { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, OffsetOf(MeshRenderVertex, normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
       { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, OffsetOf(MeshRenderVertex, texcoord), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
       { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, OffsetOf(MeshRenderVertex, color), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
   };

   D3D12_RASTERIZER_DESC rasterizerDesc = {};
   {
      rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID; /*D3D12_FILL_MODE */
      rasterizerDesc.CullMode = (psoFlags & PSOFlags_NoCull) ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK; /*D3D12_CULL_MODE */
      rasterizerDesc.FrontCounterClockwise = false; /*BOOL */
      rasterizerDesc.DepthBias = 0; /*INT */
      rasterizerDesc.DepthBiasClamp = 0.0f; /*FLOAT */
      rasterizerDesc.SlopeScaledDepthBias = 0.0f; /*FLOAT */
      rasterizerDesc.DepthClipEnable = true; /*BOOL */
      rasterizerDesc.MultisampleEnable = true; /*BOOL */
      rasterizerDesc.AntialiasedLineEnable = false; /*BOOL */
      rasterizerDesc.ForcedSampleCount = 0; /*UINT */
      rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF; /*D3D12_CONSERVATIVE_RASTERIZATION_MODE */
   }

   D3D12_RENDER_TARGET_BLEND_DESC rtvBlendDesc = {};
   if ((psoFlags & PSOFlags_AlphaBlend))
   {
      rtvBlendDesc.BlendEnable = true; /*BOOL */
      rtvBlendDesc.LogicOpEnable = false; /*BOOL */
      rtvBlendDesc.SrcBlend = D3D12_BLEND_ONE; /*D3D12_BLEND */
      rtvBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA ; /*D3D12_BLEND */
      rtvBlendDesc.BlendOp = D3D12_BLEND_OP_ADD; /*D3D12_BLEND_OP */
      rtvBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE; /*D3D12_BLEND */
      rtvBlendDesc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA; /*D3D12_BLEND */
      rtvBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD; /*D3D12_BLEND_OP */
      rtvBlendDesc.LogicOp = D3D12_LOGIC_OP_SET ; /*D3D12_LOGIC_OP */
      rtvBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; /*UINT8 */
   }
   else
   {
      rtvBlendDesc.BlendEnable = false; /*BOOL */
      rtvBlendDesc.LogicOpEnable = false; /*BOOL */
      rtvBlendDesc.SrcBlend = D3D12_BLEND_ONE; /*D3D12_BLEND */
      rtvBlendDesc.DestBlend = D3D12_BLEND_ZERO ; /*D3D12_BLEND */
      rtvBlendDesc.BlendOp = D3D12_BLEND_OP_ADD; /*D3D12_BLEND_OP */
      rtvBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE; /*D3D12_BLEND */
      rtvBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO; /*D3D12_BLEND */
      rtvBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD; /*D3D12_BLEND_OP */
      rtvBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP ; /*D3D12_LOGIC_OP */
      rtvBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; /*UINT8 */
   }

   D3D12_BLEND_DESC blendDesc = {};
   {
       blendDesc.AlphaToCoverageEnable = false; /*BOOL*/
       blendDesc.IndependentBlendEnable = false; /*BOOL*/
       blendDesc.RenderTarget[0] = rtvBlendDesc; /*D3D12_RENDER_TARGET_BLEND_DESC*/
   };


   D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
   {
      psoDesc.InputLayout = { inputElementDescs, ArrayCount(inputElementDescs) };
      psoDesc.pRootSignature = r->graphicsRootSignature;
      psoDesc.VS = D3D12_SHADER_BYTECODE{vertexByteCode, szVertexByteCode};
      psoDesc.PS = D3D12_SHADER_BYTECODE{pixelByteCode, szPixelByteCode };
      psoDesc.RasterizerState = rasterizerDesc;
      psoDesc.BlendState = blendDesc;
      psoDesc.DepthStencilState.DepthEnable = !(psoFlags & PSOFlags_NoDepth);
      psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
      psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
      psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
      psoDesc.DepthStencilState.StencilEnable = FALSE;
      psoDesc.DepthStencilState.StencilReadMask = 0;
      psoDesc.DepthStencilState.StencilWriteMask = 0;

      D3D12_DEPTH_STENCILOP_DESC depthStencilOp = {};
      depthStencilOp.StencilFailOp = D3D12_STENCIL_OP_KEEP;
      depthStencilOp.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
      depthStencilOp.StencilPassOp = D3D12_STENCIL_OP_KEEP;
      depthStencilOp.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

      psoDesc.DepthStencilState.FrontFace = depthStencilOp;
      psoDesc.DepthStencilState.BackFace = depthStencilOp;

      psoDesc.SampleMask = UINT_MAX;
      psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      psoDesc.NumRenderTargets = 1;
      psoDesc.RTVFormats[0] = (psoFlags & PSOFlags_R8FloatTarget) ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
      psoDesc.SampleDesc.Count = (psoFlags & PSOFlags_MSAA) ? gKnobs.MSSampleCount : 1;
   }

   DX12(r->device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));

   u64 idx = arrlen(r->sPSOs);
   arrpush(r->sPSOs, pso);

   popApiLifetime();

   PipelineStateHandle h{idx, PipelineStateHandle_ComputeGraphics};;
   return h;
}

PipelineStateHandle
gpuComputePipelineState(u8* code, u64 sz)
{
   Renderer* r = gRenderCore;

   u64 idx = arrlen(r->sPSOs);

   {
      ID3D12PipelineState* pso = {};

      D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
      psoDesc.pRootSignature = r->computeRootSignature;
      psoDesc.CS = D3D12_SHADER_BYTECODE{code, sz};

      DX12(r->device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso)));

      arrpush(r->sPSOs, pso);
   }
   PipelineStateHandle h{idx, PipelineStateHandle_ComputeGraphics};

   return h;
}


BLASHandle
gpuMakeBLAS(ResourceHandle vertexBuffer, u64 numVerts, u64 vertexStride, ResourceHandle indexBuffer, u64 numIndices)
{
   Renderer* r = gRenderCore;

   auto* geometryDesc = AllocateElem(D3D12_RAYTRACING_GEOMETRY_DESC, Lifetime_App); // TODO: figure out BLAS lifetime.

   geometryDesc->Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
   geometryDesc->Triangles.VertexBuffer.StartAddress = getResource(vertexBuffer)->GetGPUVirtualAddress();
   geometryDesc->Triangles.VertexBuffer.StrideInBytes = vertexStride;
   geometryDesc->Triangles.VertexCount = numVerts;
   geometryDesc->Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
   geometryDesc->Triangles.IndexBuffer = getResource(indexBuffer)->GetGPUVirtualAddress();
   geometryDesc->Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
   geometryDesc->Triangles.IndexCount = static_cast<UINT>(numIndices);
   geometryDesc->Triangles.Transform3x4 = 0;
   geometryDesc->Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

   D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS asIn = {};
   asIn.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
   asIn.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
   asIn.pGeometryDescs = geometryDesc;
   asIn.NumDescs = 1;
   asIn.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO preBuild = {};
   r->dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&asIn, &preBuild);

   preBuild.ScratchDataSizeInBytes = AlignPow2(preBuild.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
   preBuild.ResultDataMaxSizeInBytes = AlignPow2(preBuild.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

   ResourceHandle scratch = gpuCreateResource(
      preBuild.ScratchDataSizeInBytes,
      "BLAS Scratch buffer",
      GPUHeapType_Default,
      ResourceState_UnorderedAccess,
      /*alignment*/0,
      CreateResourceFlags_AllowUnordered);

   gpuMarkFreeResource(scratch, r->frameCount);

   ResourceHandle bvhRes = gpuCreateResource(
      preBuild.ResultDataMaxSizeInBytes,
      "BLAS buffer",
      GPUHeapType_Default,
      ResourceState_AccelerationStructure,
      0,
      CreateResourceFlags_AllowUnordered);

   D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
   buildDesc.Inputs = asIn;
   buildDesc.ScratchAccelerationStructureData = getResource(scratch)->GetGPUVirtualAddress();
   buildDesc.DestAccelerationStructureData = getResource(bvhRes)->GetGPUVirtualAddress();

   gpuBarrierForResource(indexBuffer, ResourceState_IndexBuffer, ResourceState_NonPixelShader);
   gpuBarrierForResource(vertexBuffer, ResourceState_VertexBuffer, ResourceState_NonPixelShader);

   r->commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

   gpuUAVBarrier(bvhRes);

   gpuBarrierForResource(indexBuffer, ResourceState_NonPixelShader, ResourceState_IndexBuffer);
   gpuBarrierForResource(vertexBuffer, ResourceState_NonPixelShader, ResourceState_VertexBuffer);

   BLAS blas = {};
   blas.blasRes = bvhRes;

   u64 blasIdx = arrlen(r->sBLAS);
   SBPush(r->sBLAS, blas, Lifetime_App);

   return BLASHandle{blasIdx};
}

// TODO: Rename these

TLAS*
gpuCreateTLAS(u64 maxNumInstances)
{
   Renderer* r = gRenderCore;

   TLAS* bvh = AllocateElem(TLAS, Lifetime_Frame);
   // NVIDIA guidelines say it's better to re-create the TLAS rather than updating it.
   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

   bvh->asIn.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
   bvh->asIn.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
   bvh->asIn.Flags = buildFlags;
   if (maxNumInstances > 0) {
      ResourceHandle instanceRes;
      instanceRes = gpuCreateResource(
               sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * maxNumInstances,
               "TLAS instance descriptions");
      gpuMarkFreeResource(instanceRes, r->frameCount);

      bvh->instancesRes = instanceRes;
      bvh->asIn.InstanceDescs = getResource(bvh->instancesRes)->GetGPUVirtualAddress();
   }
   bvh->asIn.NumDescs = 0;

   return bvh;
}

void
gpuAppendToTLAS(TLAS* bvh, BLASHandle h, mat4 transform)
{
   Renderer* r = gRenderCore;
   BLAS* blas = r->sBLAS + h.idx;

   int instanceIdx = bvh->asIn.NumDescs++;

   D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
   instanceDesc.InstanceID = instanceIdx;
   instanceDesc.InstanceContributionToHitGroupIndex = 0;  // TODO: ??
   instanceDesc.InstanceMask = 0xFF;
   instanceDesc.AccelerationStructure = getResource(blas->blasRes)->GetGPUVirtualAddress();
   instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;

   // Destination matrix is 3x4, row-major instance-to-world transform
   for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 4; ++j) {
         instanceDesc.Transform[i][j] = transform[j][i];  // We're column-major
      }
   }
   gpuSetResourceDataAtOffset(
      bvh->instancesRes,
      &instanceDesc, sizeof(instanceDesc),
      instanceIdx * sizeof(instanceDesc));
}

void
gpuBuildTLAS(TLAS* bvh)
{
   Renderer* r = gRenderCore;

   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO preBuild = {};
   r->dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bvh->asIn, &preBuild);

   preBuild.ResultDataMaxSizeInBytes = AlignPow2(preBuild.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
   preBuild.ScratchDataSizeInBytes = AlignPow2(preBuild.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
   preBuild.UpdateScratchDataSizeInBytes = AlignPow2(preBuild.UpdateScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

   ResourceHandle scratchRes = gpuCreateResource(
      Max(preBuild.ScratchDataSizeInBytes, preBuild.UpdateScratchDataSizeInBytes),
      "TLAS Scratch buffer",
      GPUHeapType_Default,
      ResourceState_UnorderedAccess,
      Max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT),
      CreateResourceFlags_AllowUnordered);
   gpuMarkFreeResource(scratchRes, r->frameCount);

   ResourceHandle dxrTlasRes = gpuCreateResource(
                        preBuild.ResultDataMaxSizeInBytes,
                        "TLAS Buffer",
                        GPUHeapType_Default,
                        ResourceState_AccelerationStructure,
                        Max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT),
                        CreateResourceFlags_AllowUnordered);

   D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
   srvDesc.Format = DXGI_FORMAT_UNKNOWN;
   srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
   srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
   srvDesc.RaytracingAccelerationStructure.Location = getResource(dxrTlasRes)->GetGPUVirtualAddress();
   r->device->CreateShaderResourceView(nullptr, &srvDesc, gpuGetSRVDescriptorCPU(0)); // Raytracing BVHs live right at the start of the descriptor heap.

   D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
   buildDesc.Inputs = bvh->asIn;
   buildDesc.ScratchAccelerationStructureData = getResource(scratchRes)->GetGPUVirtualAddress();
   buildDesc.DestAccelerationStructureData = getResource(dxrTlasRes)->GetGPUVirtualAddress();
   gpuMarkFreeResource(dxrTlasRes, gpu()->frameCount);

   r->commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

   gpuUAVBarrier(dxrTlasRes);
}

void
gpuMarkFreeResource(ResourceHandle& res, u64 atFrame)
{
   Renderer* r = gRenderCore;

   if (res.idx != 0) {
      Renderer::ToFree tf = {res, atFrame};
      if (gKnobs.gpuDebug) {
         for (int i = 0; i < SBCount(r->sToFree); ++i) {
            if (r->sToFree[i].h.idx == res.idx) {
               __debugbreak(); // Found duplicate! Double free?
            }
         }
      }
      SBPush(r->sToFree, tf, Lifetime_App);
   }
}

void
gpuMarkFreeBLAS(BLASHandle h, u64 atFrame)
{
   BLAS* b = gRenderCore->sBLAS + h.idx;
   gpuMarkFreeResource(b->blasRes, atFrame);
}

void
gpuMarkFreeColorTarget(RenderTarget* t, u64 atFrame)
{
   Renderer* r = gRenderCore;
   SBPush(r->sFreeRTVHandles, t->descriptorHandle, Lifetime_App);

   // TODO: Free-up descriptor for render target

   gpuMarkFreeResource(t->resourceHandle, atFrame);
}

void
gpuMarkFreeDepthTarget(RenderTarget* t, u64 atFrame)
{
   Renderer* r = gRenderCore;
   SBPush(r->sFreeDSVHandles, t->descriptorHandle, Lifetime_App);

   // TODO: Free-up descriptor for depth render target

   gpuMarkFreeResource(t->resourceHandle, atFrame);
}

void
gpuFreeMarkedResources()
{
   Renderer* r = gRenderCore;

   u64 completedFrame = r->frameFence->GetCompletedValue();
   if (~completedFrame == 0) {
      return;  // TDR
   }

   logMsg("Deleting freed resources for frame %d\n", completedFrame);
   for (sz i = 0; i < arrlen(r->sToFree); ++i) {
      if (r->sToFree[i].frameInTheFuture > completedFrame) {
         break;
      }
      ID3D12Resource* res = getResource(r->sToFree[i].h);
      if (res) { res->Release(); }
      SBPush(r->sFreeResHandles, r->sToFree[i].h, Lifetime_App);
      arrdelswap(r->sToFree, i--);
   }
}

void
gpuFinishInit()
{
   Renderer* r = gRenderCore;
   // Execute commands
   r->commandList->Close();
   r->commandQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&r->commandList);

   // Next frame. First frame is initialization
   DX12 ( r->commandQueue->Signal(r->frameFence, r->frameCount) );
   if (r->frameFence->GetCompletedValue() < r->frameCount) {
      DX12( r->frameFence->SetEventOnCompletion(r->frameCount, r->frameFenceEvent) );
      WaitForSingleObject(r->frameFenceEvent, INFINITE);
   }
   r->frameCount++;
}

void
gpuPrepareForMainLoop()
{
   Renderer* r = gRenderCore;

   logMsg("Resetting command list for frame %d\n", gpu()->frameCount);

   DX12( r->commandAllocator[r->frameIdxModulo]->Reset() );
   DX12( r->commandList->Reset(r->commandAllocator[r->frameIdxModulo], /*pso*/nullptr) );

   // Map current upload heap.
   r->uploadHeapUsedBytes = 0;
   DX12( r->uploadHeaps[r->frameIdxModulo]->Map(0, &D3D12_RANGE{0, 0}, reinterpret_cast<void**>(&r->uploadHeapPtr)) );
}

void
gpuWaitForFrame(u64 frame)
{
   Renderer* r = gRenderCore;

   // Wait for the previous frame to have completed rendering.
   if (r->frameFence->GetCompletedValue() < frame) {
      logMsg("Waiting for frame %d...", frame);
      DX12( r->frameFence->SetEventOnCompletion(frame, r->frameFenceEvent) );
      u64 beginUs = r->plat->getMicroseconds();
      WaitForSingleObject(r->frameFenceEvent, INFINITE);
      u64 endUs = r->plat->getMicroseconds();
      u64 diff = endUs - beginUs;
      logMsg("Waited for %dus (%f ms)\n", diff, (float)diff / 1000.0f);
      Assert (r->frameFence->GetCompletedValue() == frame);
   }
}

void
gpuFlush()
{
   Renderer* r = gRenderCore;

   DX12(r->commandQueue->Signal(r->frameFence, r->frameCount));
   gpuWaitForFrame(r->frameCount++);
}


RenderTarget*
gpuBackbuffer()
{
   Renderer* r = gRenderCore;
   return r->rtResolve[r->frameIdxModulo];
}

RenderTarget*
gpuDefaultDepthTarget()
{
   Renderer* r = gRenderCore;

   return r->rtDepth[r->frameIdxModulo];
}

void
gpuSetRenderTargets(RenderTarget* rtColor, RenderTarget* rtDepth)
{
   Renderer* r = gRenderCore;

   int rtCount = rtColor? 1 : 0;

   r->commandList->OMSetRenderTargets(rtCount, rtColor ? &rtColor->descriptorHandle : NULL, FALSE, rtDepth ? &rtDepth->descriptorHandle : NULL);
}

vec3
gpuSetClearColor(float r, float g, float b)
{
   vec3 old = gRenderCore->clearColor;
   gRenderCore->clearColor = { r, g, b };
   return old;
}

void
gpuClearRenderTargets(RenderTarget* color, RenderTarget* depth)
{
   Renderer* r = gRenderCore;

   if (depth) {
      D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = depth->descriptorHandle;
      r->commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
   }
   if (color) {
      D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = color->descriptorHandle;
      r->commandList->ClearRenderTargetView(rtvHandle, r->clearColor.data, 0, nullptr);
   }
}

ResourceHandle
gpuRenderTargetResource(RenderTarget* rt)
{
   return rt->resourceHandle;
}

u64
gpuRenderTargetBindIndex(RenderTarget* rt)
{
   Assert(rt->creationFlags & RTFlags_WithSRV);
   return rt->bindIndexSRV;
}

void
gpuSetViewport(int x, int y, int w, int h)
{
   Renderer* r = gRenderCore;
   r->commandList->RSSetViewports(1, &D3D12_VIEWPORT{(float)x, (float)y, (float)w, (float)h, 0.0f, 1.0f});
   r->commandList->RSSetScissorRects(1, &D3D12_RECT{x, y, w, h});
}

void
gpuResolve(RenderTarget* source, RenderTarget* dest)
{
   Renderer* r = gRenderCore;

   r->commandList->ResolveSubresource(
      /*dest*/getResource(gpuRenderTargetResource(dest)),
      /*subresource*/0,
      getResource(gpuRenderTargetResource(source)),
      0,
      DXGI_FORMAT_R8G8B8A8_UNORM);
}

void
gpuBeginRenderTick()
{
   Renderer* r = gRenderCore;
   logMsg("Sending commands for frame %d\n", r->frameCount);

   r->commandList->SetGraphicsRootSignature(r->graphicsRootSignature);
   r->commandList->SetComputeRootSignature(r->computeRootSignature);

   r->commandList->SetDescriptorHeaps(1, &r->grvHeap);
   r->commandList->SetComputeRootDescriptorTable(3, r->grvHeap->GetGPUDescriptorHandleForHeapStart());
   r->commandList->SetComputeRootDescriptorTable(4, r->grvHeap->GetGPUDescriptorHandleForHeapStart());
   r->commandList->SetComputeRootDescriptorTable(5, r->grvHeap->GetGPUDescriptorHandleForHeapStart());
   r->commandList->SetComputeRootDescriptorTable(6, r->grvHeap->GetGPUDescriptorHandleForHeapStart());

   r->commandList->SetGraphicsRootDescriptorTable(3, r->grvHeap->GetGPUDescriptorHandleForHeapStart());
   r->commandList->SetGraphicsRootDescriptorTable(4, r->grvHeap->GetGPUDescriptorHandleForHeapStart());
   r->commandList->SetGraphicsRootDescriptorTable(5, r->grvHeap->GetGPUDescriptorHandleForHeapStart());
   r->commandList->SetGraphicsRootDescriptorTable(6, r->grvHeap->GetGPUDescriptorHandleForHeapStart());

   r->commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void
gpuEndRenderTick()
{
   Renderer* r = gRenderCore;

   logMsg("Executing command list for frame %d\n", r->frameCount);

   r->commandList->Close();
   r->commandQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&r->commandList);

   if (FAILED(r->device->GetDeviceRemovedReason())) {
      handleDx12Error("Device removed.");
   }

   // Signal current frame
   DX12 ( r->commandQueue->Signal(r->frameFence, r->frameCount) );

   // We are rendering directly to the back buffer in our swap chain, so we
   // wait for it before presenting.
   gpuWaitForFrame(r->frameCount);

   gpuFreeMarkedResources();

   logMsg("Presenting frame %d...",  r->frameCount, r->swapChain->GetCurrentBackBufferIndex());
   u64 presentBeforeUs = r->plat->getMicroseconds();
   // Advance to next frame.
   r->frameCount++;

   // Unmap upload heap
   r->uploadHeaps[r->frameIdxModulo]->Unmap(0, &D3D12_RANGE{0, r->uploadHeapUsedBytes});

   switch ( r->swapChain->Present(gKnobs.syncInterval, 0) ) {
      case S_OK: { } break;
      case DXGI_ERROR_DEVICE_RESET: {
         handleDx12Error("Device reset.");
      } break;
      case DXGI_ERROR_DEVICE_REMOVED: {
         handleDx12Error("Device removed.");
      } break;
   }

   u64 presentAfterUs = r->plat->getMicroseconds();
   u64 diff = presentAfterUs - presentBeforeUs;

   logMsg("Presented in %d us (%fms)...\n",  diff, diff/1000.0f);


   r->frameIdxModulo = r->swapChain->GetCurrentBackBufferIndex();
}

void
gpuSetPipelineState(PipelineStateHandle pso)
{
   // Assert(pso.type == PipelineStateHandle_ComputeGraphics);

   Renderer* r = gRenderCore;
   r->dxrCurrentPipeline = pso;
   r->commandList->SetPipelineState(r->sPSOs[pso.idx]);
}

void
gpuSetVertexAndIndexBuffers(ResourceHandle vertexBuffer, u64 vertexBytes, ResourceHandle indexBuffer, u64 indexBytes)
{
   Renderer* r = gRenderCore;

   D3D12_VERTEX_BUFFER_VIEW vbv = {};
   D3D12_INDEX_BUFFER_VIEW ibv = {};

   vbv.BufferLocation = getResource(vertexBuffer)->GetGPUVirtualAddress();
   vbv.StrideInBytes = sizeof(MeshRenderVertex);
   vbv.SizeInBytes = vertexBytes;

   ibv.BufferLocation = getResource(indexBuffer)->GetGPUVirtualAddress();
   ibv.SizeInBytes = indexBytes;
   ibv.Format = DXGI_FORMAT_R32_UINT;


   r->commandList->IASetVertexBuffers(0, 1, &vbv);
   r->commandList->IASetIndexBuffer(&ibv);
}

void
gpuSetComputeConstantSlot(int slot, ResourceHandle resource)
{
   Renderer* r = gRenderCore;
   r->commandList->SetComputeRootConstantBufferView(0, getResource(resource)->GetGPUVirtualAddress());
}

void
gpuSetGraphicsConstantSlot(int slot, ResourceHandle resource)
{
   Renderer* r = gRenderCore;
   r->commandList->SetGraphicsRootConstantBufferView(slot, getResource(resource)->GetGPUVirtualAddress());
}

void
gpuDrawIndexed(u64 numIndices)
{
   Renderer* r = gRenderCore;
   r->commandList->DrawIndexedInstanced(numIndices, 1, 0, 0, 0);
}

// For reference for when we re-introduce blobs..
#if 0
void
renderBlobDraft(Camera& cam)
{
   Renderer* r = gRenderCore;
   WorldRender& wr = gWorldRender;
      {
         r->commandList->SetPipelineState(r->hmPSOs[wr.sdfUpdatePSO].value);
         r->commandList->SetComputeRootConstantBufferView(1, getResource(cmd.blobHandle.constantResource)->GetGPUVirtualAddress());
         r->commandList->Dispatch(TempNumVoxels / (16), TempNumVoxels / (16), TempNumVoxels / (4));
      }
      gpuBarrierForResource(getResource(sdf->resourceHandle), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

      r->commandList->SetPipelineState(r->hmPSOs[wr.sdfDrawPSO].value);

      D3D12_VERTEX_BUFFER_VIEW vbv = {};
      D3D12_INDEX_BUFFER_VIEW ibv = {};
      u64 numIndices = gpuGetMeshBuffers(wr.blobQuad, vbv, ibv);
      r->commandList->IASetVertexBuffers(0, 1, &vbv);
      r->commandList->IASetIndexBuffer(&ibv);

      Material& material = wr.sMaterials[cmd.blobHandle.materialHandle];
      ResourceHandle materialRes = material.gpuResource;

      vec3 viewDir = normalized(cam->lookat - cam->eye);

      MaterialConstantsCB& mcb = material.constants;
      mcb.objectTransform = mat4Identity();
      mcb.viewProjection = mat4Identity();  // Screen space quad.
      mcb.toWorld = mat4Inverse(viewProjection);
      mcb.near = cam->near;
      mcb.far = cam->far;
      mcb.camDir = viewDir;
      mcb.camPos = cam->eye;
      gpuSetResourceData(materialRes, &mcb, sizeof(mcb));

      SDFReadCB sdfcb = {};
      sdfcb.texBindIdx = sdf->bindIndexSRV;
      sdfcb.numVoxels = vec3{TempNumVoxels, TempNumVoxels, TempNumVoxels};
      sdfcb.worldDim = vec3{TempWorldDim, TempWorldDim, TempWorldDim};
      ResourceHandle sdfRes = gpuCreateResource(sizeof(sdfcb), "SDF constant buffer");
      gpuMarkFreeResource(sdfRes, gpu()->frameCount);
      gpuSetResourceData(sdfRes, &sdfcb, sizeof(sdfcb));

      r->commandList->SetGraphicsRootConstantBufferView(0, getResource(sdfRes)->GetGPUVirtualAddress());
      r->commandList->SetGraphicsRootConstantBufferView(1, getResource(materialRes)->GetGPUVirtualAddress());
      r->commandList->DrawIndexedInstanced(numIndices, 1, 0, 0, 0);
}
#endif

void
gpuFreeAppResources()
{
   Renderer* r = gRenderCore;

   for (sz i = 0; i < arrlen(r->sAppResources); ++i) {
      ID3D12Resource* res = getResource(r->sAppResources[i]);
      if (res) { res->Release(); }

   }
}

void
gpuDispose()
{
   Renderer* r = gRenderCore;
   gpuFlush();
   gpuFreeMarkedResources();
   ID3D12DebugDevice* debugInterface;
   if (SUCCEEDED(r->device->QueryInterface(&debugInterface))) {
       debugInterface->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
       debugInterface->Release();
   }
}