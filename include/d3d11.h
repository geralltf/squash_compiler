#ifndef _D3D11_H
#define _D3D11_H
/* Minimal Direct3D 11 surface — enough of the real d3d11.h to create a
 * device+swapchain, compile+bind vertex/pixel shaders, build an input
 * layout, upload vertex/constant buffers, and draw. Hand-written to match
 * the real ABI (exact vtable order/count up through the last method this
 * project actually calls) since this calls into the genuine system
 * d3d11.dll, not a fake implementation — see dxgi.h's identical note.
 * Trailing vtable slots this project never calls (of which the real
 * interfaces have many) are simply not declared; that only limits what we
 * can see through this header, not the real object's binary layout. */
#include "include/windows.h"
#include "include/dxgi.h"

typedef enum D3D_DRIVER_TYPE {
    D3D_DRIVER_TYPE_UNKNOWN = 0,
    D3D_DRIVER_TYPE_HARDWARE = 1,
    D3D_DRIVER_TYPE_REFERENCE = 2,
    D3D_DRIVER_TYPE_NULL = 3,
    D3D_DRIVER_TYPE_SOFTWARE = 4,
    D3D_DRIVER_TYPE_WARP = 5
} D3D_DRIVER_TYPE;

typedef enum D3D_FEATURE_LEVEL {
    D3D_FEATURE_LEVEL_9_1 = 0x9100,
    D3D_FEATURE_LEVEL_9_2 = 0x9200,
    D3D_FEATURE_LEVEL_9_3 = 0x9300,
    D3D_FEATURE_LEVEL_10_0 = 0xa000,
    D3D_FEATURE_LEVEL_10_1 = 0xa100,
    D3D_FEATURE_LEVEL_11_0 = 0xb000,
    D3D_FEATURE_LEVEL_11_1 = 0xb100
} D3D_FEATURE_LEVEL;

typedef enum D3D_PRIMITIVE_TOPOLOGY {
    D3D_PRIMITIVE_TOPOLOGY_UNDEFINED = 0,
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST = 4
} D3D_PRIMITIVE_TOPOLOGY;

#define D3D11_SDK_VERSION 7
#define D3D11_CREATE_DEVICE_DEBUG 0x2

/* --- Buffers / resources --- */
typedef enum D3D11_USAGE {
    D3D11_USAGE_DEFAULT = 0,
    D3D11_USAGE_IMMUTABLE = 1,
    D3D11_USAGE_DYNAMIC = 2,
    D3D11_USAGE_STAGING = 3
} D3D11_USAGE;

#define D3D11_BIND_VERTEX_BUFFER   0x1L
#define D3D11_BIND_INDEX_BUFFER    0x2L
#define D3D11_BIND_CONSTANT_BUFFER 0x4L

#define D3D11_CPU_ACCESS_WRITE 0x10000L
#define D3D11_CPU_ACCESS_READ  0x20000L

typedef struct D3D11_BUFFER_DESC {
    UINT ByteWidth;
    D3D11_USAGE Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
    UINT StructureByteStride;
} D3D11_BUFFER_DESC;

typedef struct D3D11_TEXTURE2D_DESC {
    UINT Width;
    UINT Height;
    UINT MipLevels;
    UINT ArraySize;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D11_USAGE Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
} D3D11_TEXTURE2D_DESC;

typedef struct D3D11_MAPPED_SUBRESOURCE {
    void *pData;
    UINT RowPitch;
    UINT DepthPitch;
} D3D11_MAPPED_SUBRESOURCE;
#define D3D11_MAP_READ 1

typedef struct D3D11_SUBRESOURCE_DATA {
    const void *pSysMem;
    UINT SysMemPitch;
    UINT SysMemSlicePitch;
} D3D11_SUBRESOURCE_DATA;

typedef struct D3D11_INPUT_ELEMENT_DESC {
    LPCSTR SemanticName;
    UINT SemanticIndex;
    DXGI_FORMAT Format;
    UINT InputSlot;
    UINT AlignedByteOffset;
    UINT InputSlotClass; /* D3D11_INPUT_CLASSIFICATION, 0 = per-vertex */
    UINT InstanceDataStepRate;
} D3D11_INPUT_ELEMENT_DESC;
#define D3D11_APPEND_ALIGNED_ELEMENT 0xffffffffU
#define D3D11_INPUT_PER_VERTEX_DATA 0

typedef struct D3D11_VIEWPORT {
    float TopLeftX;
    float TopLeftY;
    float Width;
    float Height;
    float MinDepth;
    float MaxDepth;
} D3D11_VIEWPORT;

/* Opaque resource/state object interfaces: this demo only ever calls
 * Release (via IUnknown) on these, never any type-specific method, so each
 * gets its own distinct struct+Vtbl (identical in content to IUnknownVtbl)
 * rather than "typedef IUnknown X;". A plain typedef-alias-of-another-
 * named-struct-type here triggers a real squash codegen bug: "x->lpVtbl->
 * Release(x)" resolves incorrectly (crashes) when x's declared type is a
 * bare alias of IUnknown, but works fine when it's a genuinely distinct
 * struct that merely shares IUnknown's first three vtable slots (confirmed
 * via isolated repro: Release() on an IDXGISwapChain* — its own distinct
 * Vtbl struct — works; Release() on an ID3D11RenderTargetView* declared as
 * "typedef IUnknown ID3D11RenderTargetView;" crashes with the exact same
 * call shape). Giving every interface its own Vtbl struct sidesteps the bug
 * entirely and also matches how real d3d11.h actually declares them. */
#define SQUASH_DECLARE_IUNKNOWN_LIKE(Name) \
    typedef struct Name Name; \
    typedef struct Name##Vtbl { \
        HRESULT (WINAPI *QueryInterface)(Name *self, REFIID riid, void **ppvObject); \
        ULONG   (WINAPI *AddRef)(Name *self); \
        ULONG   (WINAPI *Release)(Name *self); \
    } Name##Vtbl; \
    struct Name { Name##Vtbl *lpVtbl; }

SQUASH_DECLARE_IUNKNOWN_LIKE(ID3D11Buffer);
SQUASH_DECLARE_IUNKNOWN_LIKE(ID3D11VertexShader);
SQUASH_DECLARE_IUNKNOWN_LIKE(ID3D11PixelShader);
SQUASH_DECLARE_IUNKNOWN_LIKE(ID3D11InputLayout);
SQUASH_DECLARE_IUNKNOWN_LIKE(ID3D11RenderTargetView);
/* ID3D11Texture2D needs its own full vtable (not the generic IUnknown-only
 * stub) so GetDesc is reachable -- used to query a back buffer's REAL
 * dimensions/format directly from the driver, for verifying render output
 * without trusting assumed values. Real order: 0-2 IUnknown, 3 GetDevice,
 * 4 GetPrivateData, 5 SetPrivateData, 6 SetPrivateDataInterface, 7 GetType,
 * 8 SetEvictionPriority, 9 GetEvictionPriority, 10 GetDesc. */
typedef struct ID3D11Texture2D ID3D11Texture2D;
typedef struct ID3D11Texture2DVtbl {
    HRESULT (WINAPI *QueryInterface)(ID3D11Texture2D *self, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(ID3D11Texture2D *self);
    ULONG   (WINAPI *Release)(ID3D11Texture2D *self);
    void    (WINAPI *GetDevice)(ID3D11Texture2D *self, void **ppDevice);
    HRESULT (WINAPI *GetPrivateData)(ID3D11Texture2D *self, REFGUID guid, UINT *pDataSize, void *pData);
    HRESULT (WINAPI *SetPrivateData)(ID3D11Texture2D *self, REFGUID guid, UINT DataSize, const void *pData);
    HRESULT (WINAPI *SetPrivateDataInterface)(ID3D11Texture2D *self, REFGUID guid, const IUnknown *pData);
    void    (WINAPI *GetType)(ID3D11Texture2D *self, UINT *pResourceDimension);
    void    (WINAPI *SetEvictionPriority)(ID3D11Texture2D *self, UINT EvictionPriority);
    UINT    (WINAPI *GetEvictionPriority)(ID3D11Texture2D *self);
    void    (WINAPI *GetDesc)(ID3D11Texture2D *self, void *pDesc);
} ID3D11Texture2DVtbl;
struct ID3D11Texture2D { ID3D11Texture2DVtbl *lpVtbl; };
SQUASH_DECLARE_IUNKNOWN_LIKE(ID3D11Resource);
SQUASH_DECLARE_IUNKNOWN_LIKE(ID3D11RasterizerState);

/* D3D11's default rasterizer state (used whenever no state is explicitly
 * bound) has CullMode=D3D11_CULL_BACK and FrontCounterClockwise=FALSE
 * (clockwise-in-screen-space is front-facing). A real, common source of "my
 * triangle doesn't show up but everything reports success" bugs: get the
 * vertex winding backwards (easy to do, since NDC is Y-up but screen space
 * is Y-down, flipping apparent winding) and the whole triangle is silently
 * back-face-culled every frame. Declaring this lets a demo just disable
 * culling instead of having to reason about winding at all. */
typedef enum D3D11_FILL_MODE { D3D11_FILL_WIREFRAME = 2, D3D11_FILL_SOLID = 3 } D3D11_FILL_MODE;
typedef enum D3D11_CULL_MODE { D3D11_CULL_NONE = 1, D3D11_CULL_FRONT = 2, D3D11_CULL_BACK = 3 } D3D11_CULL_MODE;

typedef struct D3D11_RASTERIZER_DESC {
    D3D11_FILL_MODE FillMode;
    D3D11_CULL_MODE CullMode;
    BOOL FrontCounterClockwise;
    INT DepthBias;
    float DepthBiasClamp;
    float SlopeScaledDepthBias;
    BOOL DepthClipEnable;
    BOOL ScissorEnable;
    BOOL MultisampleEnable;
    BOOL AntialiasedLineEnable;
} D3D11_RASTERIZER_DESC;

/* --- ID3D11DeviceContext : ID3D11DeviceChild : IUnknown ---
 * Real vtable order (indices shown for reference): 0-2 IUnknown,
 * 3 GetDevice, 4 GetPrivateData, 5 SetPrivateData, 6 SetPrivateDataInterface,
 * 7 VSSetConstantBuffers, 8 PSSetShaderResources, 9 PSSetShader,
 * 10 PSSetSamplers, 11 VSSetShader, 12 DrawIndexed, 13 Draw, 14 Map,
 * 15 Unmap, 16 PSSetConstantBuffers, 17 IASetInputLayout,
 * 18 IASetVertexBuffers, 19 IASetIndexBuffer, 20 DrawIndexedInstanced,
 * 21 DrawInstanced, 22 GSSetConstantBuffers, 23 GSSetShader,
 * 24 IASetPrimitiveTopology, 25 VSSetShaderResources, 26 VSSetSamplers,
 * 27 Begin, 28 End, 29 GetData, 30 SetPredication, 31 GSSetShaderResources,
 * 32 GSSetSamplers, 33 OMSetRenderTargets, 34 OMSetRenderTargetsAndUnordered
 * AccessViews, 35 OMSetBlendState, 36 OMSetDepthStencilState, 37 SOSetTargets,
 * 38 DrawAuto, 39 DrawIndexedInstancedIndirect, 40 DrawInstancedIndirect,
 * 41 Dispatch, 42 DispatchIndirect, 43 RSSetState, 44 RSSetViewports,
 * 45 RSSetScissorRects, 46 CopySubresourceRegion, 47 CopyResource,
 * 48 UpdateSubresource, ... (this demo needs nothing past here). */
typedef struct ID3D11DeviceContext ID3D11DeviceContext;
typedef struct ID3D11DeviceContextVtbl {
    HRESULT (WINAPI *QueryInterface)(ID3D11DeviceContext *self, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(ID3D11DeviceContext *self);
    ULONG   (WINAPI *Release)(ID3D11DeviceContext *self);
    void    (WINAPI *GetDevice)(ID3D11DeviceContext *self, void **ppDevice);
    HRESULT (WINAPI *GetPrivateData)(ID3D11DeviceContext *self, REFGUID guid, UINT *pDataSize, void *pData);
    HRESULT (WINAPI *SetPrivateData)(ID3D11DeviceContext *self, REFGUID guid, UINT DataSize, const void *pData);
    HRESULT (WINAPI *SetPrivateDataInterface)(ID3D11DeviceContext *self, REFGUID guid, const IUnknown *pData);
    void    (WINAPI *VSSetConstantBuffers)(ID3D11DeviceContext *self, UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers);
    void    (WINAPI *PSSetShaderResources)(ID3D11DeviceContext *self, UINT StartSlot, UINT NumViews, void *const *ppShaderResourceViews);
    void    (WINAPI *PSSetShader)(ID3D11DeviceContext *self, ID3D11PixelShader *pPixelShader, void *const *ppClassInstances, UINT NumClassInstances);
    void    (WINAPI *PSSetSamplers)(ID3D11DeviceContext *self, UINT StartSlot, UINT NumSamplers, void *const *ppSamplers);
    void    (WINAPI *VSSetShader)(ID3D11DeviceContext *self, ID3D11VertexShader *pVertexShader, void *const *ppClassInstances, UINT NumClassInstances);
    void    (WINAPI *DrawIndexed)(ID3D11DeviceContext *self, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation);
    void    (WINAPI *Draw)(ID3D11DeviceContext *self, UINT VertexCount, UINT StartVertexLocation);
    HRESULT (WINAPI *Map)(ID3D11DeviceContext *self, ID3D11Resource *pResource, UINT Subresource, UINT MapType, UINT MapFlags, void *pMappedResource);
    void    (WINAPI *Unmap)(ID3D11DeviceContext *self, ID3D11Resource *pResource, UINT Subresource);
    void    (WINAPI *PSSetConstantBuffers)(ID3D11DeviceContext *self, UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers);
    void    (WINAPI *IASetInputLayout)(ID3D11DeviceContext *self, ID3D11InputLayout *pInputLayout);
    void    (WINAPI *IASetVertexBuffers)(ID3D11DeviceContext *self, UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppVertexBuffers, const UINT *pStrides, const UINT *pOffsets);
    void    (WINAPI *IASetIndexBuffer)(ID3D11DeviceContext *self, ID3D11Buffer *pIndexBuffer, DXGI_FORMAT Format, UINT Offset);
    void    (WINAPI *DrawIndexedInstanced)(ID3D11DeviceContext *self, UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation);
    void    (WINAPI *DrawInstanced)(ID3D11DeviceContext *self, UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation);
    void    (WINAPI *GSSetConstantBuffers)(ID3D11DeviceContext *self, UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers);
    void    (WINAPI *GSSetShader)(ID3D11DeviceContext *self, void *pShader, void *const *ppClassInstances, UINT NumClassInstances);
    void    (WINAPI *IASetPrimitiveTopology)(ID3D11DeviceContext *self, D3D_PRIMITIVE_TOPOLOGY Topology);
    void    (WINAPI *VSSetShaderResources)(ID3D11DeviceContext *self, UINT StartSlot, UINT NumViews, void *const *ppShaderResourceViews);
    void    (WINAPI *VSSetSamplers)(ID3D11DeviceContext *self, UINT StartSlot, UINT NumSamplers, void *const *ppSamplers);
    void    (WINAPI *Begin)(ID3D11DeviceContext *self, void *pAsync);
    void    (WINAPI *End)(ID3D11DeviceContext *self, void *pAsync);
    HRESULT (WINAPI *GetData)(ID3D11DeviceContext *self, void *pAsync, void *pData, UINT DataSize, UINT GetDataFlags);
    void    (WINAPI *SetPredication)(ID3D11DeviceContext *self, void *pPredicate, BOOL PredicateValue);
    void    (WINAPI *GSSetShaderResources)(ID3D11DeviceContext *self, UINT StartSlot, UINT NumViews, void *const *ppShaderResourceViews);
    void    (WINAPI *GSSetSamplers)(ID3D11DeviceContext *self, UINT StartSlot, UINT NumSamplers, void *const *ppSamplers);
    void    (WINAPI *OMSetRenderTargets)(ID3D11DeviceContext *self, UINT NumViews, ID3D11RenderTargetView *const *ppRenderTargetViews, void *pDepthStencilView);
    void    (WINAPI *OMSetRenderTargetsAndUnorderedAccessViews)(ID3D11DeviceContext *self, UINT NumRTVs, ID3D11RenderTargetView *const *ppRenderTargetViews, void *pDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, void *const *ppUnorderedAccessViews, const UINT *pUAVInitialCounts);
    void    (WINAPI *OMSetBlendState)(ID3D11DeviceContext *self, void *pBlendState, const float BlendFactor[4], UINT SampleMask);
    void    (WINAPI *OMSetDepthStencilState)(ID3D11DeviceContext *self, void *pDepthStencilState, UINT StencilRef);
    void    (WINAPI *SOSetTargets)(ID3D11DeviceContext *self, UINT NumBuffers, ID3D11Buffer *const *ppSOTargets, const UINT *pOffsets);
    void    (WINAPI *DrawAuto)(ID3D11DeviceContext *self);
    void    (WINAPI *DrawIndexedInstancedIndirect)(ID3D11DeviceContext *self, ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs);
    void    (WINAPI *DrawInstancedIndirect)(ID3D11DeviceContext *self, ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs);
    void    (WINAPI *Dispatch)(ID3D11DeviceContext *self, UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ);
    void    (WINAPI *DispatchIndirect)(ID3D11DeviceContext *self, ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs);
    void    (WINAPI *RSSetState)(ID3D11DeviceContext *self, void *pRasterizerState);
    void    (WINAPI *RSSetViewports)(ID3D11DeviceContext *self, UINT NumViewports, const D3D11_VIEWPORT *pViewports);
    void    (WINAPI *RSSetScissorRects)(ID3D11DeviceContext *self, UINT NumRects, const void *pRects);
    void    (WINAPI *CopySubresourceRegion)(ID3D11DeviceContext *self, ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource, const void *pSrcBox);
    void    (WINAPI *CopyResource)(ID3D11DeviceContext *self, ID3D11Resource *pDstResource, ID3D11Resource *pSrcResource);
    void    (WINAPI *UpdateSubresource)(ID3D11DeviceContext *self, ID3D11Resource *pDstResource, UINT DstSubresource, const void *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch);
    void    (WINAPI *CopyStructureCount)(ID3D11DeviceContext *self, ID3D11Buffer *pDstBuffer, UINT DstAlignedByteOffset, void *pSrcView);
    void    (WINAPI *ClearRenderTargetView)(ID3D11DeviceContext *self, ID3D11RenderTargetView *pRenderTargetView, const float ColorRGBA[4]);
} ID3D11DeviceContextVtbl;
struct ID3D11DeviceContext { ID3D11DeviceContextVtbl *lpVtbl; };

/* Shader-reflection-free shader creation: only the bytecode blob + length
 * are needed to create a shader object, both real d3d11.dll entry points. */

/* --- ID3D11Device : IUnknown ---
 * Real vtable order (indices for reference): 0-2 IUnknown, 3 CreateBuffer,
 * 4 CreateTexture1D, 5 CreateTexture2D, 6 CreateTexture3D,
 * 7 CreateShaderResourceView, 8 CreateUnorderedAccessView,
 * 9 CreateRenderTargetView, 10 CreateDepthStencilView, 11 CreateInputLayout,
 * 12 CreateVertexShader, 13 CreateGeometryShader,
 * 14 CreateGeometryShaderWithStreamOutput, 15 CreatePixelShader,
 * 16 CreateHullShader, 17 CreateDomainShader, 18 CreateComputeShader,
 * 19 CreateClassLinkage, 20 CreateBlendState, 21 CreateDepthStencilState,
 * 22 CreateRasterizerState, 23 CreateSamplerState, 24 CreateQuery,
 * 25 CreatePredicate, 26 CreateCounter, 27 CreateDeferredContext,
 * 28 OpenSharedResource, 29 CheckFormatSupport,
 * 30 CheckMultisampleQualityLevels, 31 CheckCounterInfo, 32 CheckCounter,
 * 33 CheckFeatureSupport, 34 GetPrivateData, 35 SetPrivateData,
 * 36 SetPrivateDataInterface, 37 GetFeatureLevel, 38 GetCreationFlags,
 * 39 GetDeviceRemovedReason, 40 GetImmediateContext. */
typedef struct ID3D11Device ID3D11Device;
typedef struct ID3D11DeviceVtbl {
    HRESULT (WINAPI *QueryInterface)(ID3D11Device *self, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(ID3D11Device *self);
    ULONG   (WINAPI *Release)(ID3D11Device *self);
    HRESULT (WINAPI *CreateBuffer)(ID3D11Device *self, const D3D11_BUFFER_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Buffer **ppBuffer);
    HRESULT (WINAPI *CreateTexture1D)(ID3D11Device *self, const void *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, void **ppTexture1D);
    HRESULT (WINAPI *CreateTexture2D)(ID3D11Device *self, const void *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture2D **ppTexture2D);
    HRESULT (WINAPI *CreateTexture3D)(ID3D11Device *self, const void *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, void **ppTexture3D);
    HRESULT (WINAPI *CreateShaderResourceView)(ID3D11Device *self, ID3D11Resource *pResource, const void *pDesc, void **ppSRView);
    HRESULT (WINAPI *CreateUnorderedAccessView)(ID3D11Device *self, ID3D11Resource *pResource, const void *pDesc, void **ppUAView);
    HRESULT (WINAPI *CreateRenderTargetView)(ID3D11Device *self, ID3D11Resource *pResource, const void *pDesc, ID3D11RenderTargetView **ppRTView);
    HRESULT (WINAPI *CreateDepthStencilView)(ID3D11Device *self, ID3D11Resource *pResource, const void *pDesc, void **ppDepthStencilView);
    HRESULT (WINAPI *CreateInputLayout)(ID3D11Device *self, const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs, UINT NumElements, const void *pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, ID3D11InputLayout **ppInputLayout);
    HRESULT (WINAPI *CreateVertexShader)(ID3D11Device *self, const void *pShaderBytecode, SIZE_T BytecodeLength, void *pClassLinkage, ID3D11VertexShader **ppVertexShader);
    HRESULT (WINAPI *CreateGeometryShader)(ID3D11Device *self, const void *pShaderBytecode, SIZE_T BytecodeLength, void *pClassLinkage, void **ppGeometryShader);
    HRESULT (WINAPI *CreateGeometryShaderWithStreamOutput)(ID3D11Device *self, const void *pShaderBytecode, SIZE_T BytecodeLength, const void *pSODeclaration, UINT NumEntries, const UINT *pBufferStrides, UINT NumStrides, UINT RasterizedStream, void *pClassLinkage, void **ppGeometryShader);
    HRESULT (WINAPI *CreatePixelShader)(ID3D11Device *self, const void *pShaderBytecode, SIZE_T BytecodeLength, void *pClassLinkage, ID3D11PixelShader **ppPixelShader);
    HRESULT (WINAPI *CreateHullShader)(ID3D11Device *self, const void *pShaderBytecode, SIZE_T BytecodeLength, void *pClassLinkage, void **ppHullShader);
    HRESULT (WINAPI *CreateDomainShader)(ID3D11Device *self, const void *pShaderBytecode, SIZE_T BytecodeLength, void *pClassLinkage, void **ppDomainShader);
    HRESULT (WINAPI *CreateComputeShader)(ID3D11Device *self, const void *pShaderBytecode, SIZE_T BytecodeLength, void *pClassLinkage, void **ppComputeShader);
    HRESULT (WINAPI *CreateClassLinkage)(ID3D11Device *self, void **ppLinkage);
    HRESULT (WINAPI *CreateBlendState)(ID3D11Device *self, const void *pBlendStateDesc, void **ppBlendState);
    HRESULT (WINAPI *CreateDepthStencilState)(ID3D11Device *self, const void *pDepthStencilDesc, void **ppDepthStencilState);
    HRESULT (WINAPI *CreateRasterizerState)(ID3D11Device *self, const void *pRasterizerDesc, void **ppRasterizerState);
    HRESULT (WINAPI *CreateSamplerState)(ID3D11Device *self, const void *pSamplerDesc, void **ppSamplerState);
    HRESULT (WINAPI *CreateQuery)(ID3D11Device *self, const void *pQueryDesc, void **ppQuery);
    HRESULT (WINAPI *CreatePredicate)(ID3D11Device *self, const void *pPredicateDesc, void **ppPredicate);
    HRESULT (WINAPI *CreateCounter)(ID3D11Device *self, const void *pCounterDesc, void **ppCounter);
    HRESULT (WINAPI *CreateDeferredContext)(ID3D11Device *self, UINT ContextFlags, ID3D11DeviceContext **ppDeferredContext);
    HRESULT (WINAPI *OpenSharedResource)(ID3D11Device *self, HANDLE hResource, REFIID ReturnedInterface, void **ppResource);
    HRESULT (WINAPI *CheckFormatSupport)(ID3D11Device *self, DXGI_FORMAT Format, UINT *pFormatSupport);
    HRESULT (WINAPI *CheckMultisampleQualityLevels)(ID3D11Device *self, DXGI_FORMAT Format, UINT SampleCount, UINT *pNumQualityLevels);
    void    (WINAPI *CheckCounterInfo)(ID3D11Device *self, void *pCounterInfo);
    HRESULT (WINAPI *CheckCounter)(ID3D11Device *self, const void *pDesc, UINT *pType, UINT *pActiveCounters, LPSTR szName, UINT *pNameLength, LPSTR szUnits, UINT *pUnitsLength, LPSTR szDescription, UINT *pDescriptionLength);
    HRESULT (WINAPI *CheckFeatureSupport)(ID3D11Device *self, UINT Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize);
    HRESULT (WINAPI *GetPrivateData)(ID3D11Device *self, REFGUID guid, UINT *pDataSize, void *pData);
    HRESULT (WINAPI *SetPrivateData)(ID3D11Device *self, REFGUID guid, UINT DataSize, const void *pData);
    HRESULT (WINAPI *SetPrivateDataInterface)(ID3D11Device *self, REFGUID guid, const IUnknown *pData);
    D3D_FEATURE_LEVEL (WINAPI *GetFeatureLevel)(ID3D11Device *self);
    UINT    (WINAPI *GetCreationFlags)(ID3D11Device *self);
    HRESULT (WINAPI *GetDeviceRemovedReason)(ID3D11Device *self);
    void    (WINAPI *GetImmediateContext)(ID3D11Device *self, ID3D11DeviceContext **ppImmediateContext);
} ID3D11DeviceVtbl;
struct ID3D11Device { ID3D11DeviceVtbl *lpVtbl; };

/* D3D11CreateDeviceAndSwapChain — real exported entry point in d3d11.dll.
 * Also implicitly creates the underlying IDXGIFactory, so this app never
 * needs to touch DXGI factory/adapter enumeration directly. */
HRESULT WINAPI D3D11CreateDeviceAndSwapChain(
    void *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags,
    const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion,
    const DXGI_SWAP_CHAIN_DESC *pSwapChainDesc, IDXGISwapChain **ppSwapChain,
    ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel, ID3D11DeviceContext **ppImmediateContext);

/* --- ID3D11InfoQueue (d3d11sdklayers.h) : IUnknown ---
 * Only available on a device created with D3D11_CREATE_DEVICE_DEBUG. Lets a
 * program pull real validation-layer messages (parameter errors, invalid
 * state, etc.) directly in-process via QueryInterface, instead of needing
 * an external debugger attached to see OutputDebugString output -- exactly
 * the kind of authoritative diagnostic needed when "everything reports
 * S_OK but the picture is wrong". */
static const GUID IID_ID3D11InfoQueue = {0x6543dbb6, 0x1b48, 0x42f5, {0xab, 0x82, 0xe9, 0x7e, 0xc7, 0x43, 0x26, 0xf6}};

typedef enum D3D11_MESSAGE_CATEGORY { D3D11_MESSAGE_CATEGORY_UNKNOWN = 0 } D3D11_MESSAGE_CATEGORY;
typedef enum D3D11_MESSAGE_SEVERITY {
    D3D11_MESSAGE_SEVERITY_CORRUPTION = 0,
    D3D11_MESSAGE_SEVERITY_ERROR = 1,
    D3D11_MESSAGE_SEVERITY_WARNING = 2,
    D3D11_MESSAGE_SEVERITY_INFO = 3,
    D3D11_MESSAGE_SEVERITY_MESSAGE = 4
} D3D11_MESSAGE_SEVERITY;

typedef struct D3D11_MESSAGE {
    D3D11_MESSAGE_CATEGORY Category;
    D3D11_MESSAGE_SEVERITY Severity;
    INT ID;
    const char *pDescription;
    SIZE_T DescriptionByteLength;
} D3D11_MESSAGE;

typedef struct ID3D11InfoQueue ID3D11InfoQueue;
typedef struct ID3D11InfoQueueVtbl {
    HRESULT (WINAPI *QueryInterface)(ID3D11InfoQueue *self, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(ID3D11InfoQueue *self);
    ULONG   (WINAPI *Release)(ID3D11InfoQueue *self);
    HRESULT (WINAPI *SetMessageCountLimit)(ID3D11InfoQueue *self, UINT64 MessageCountLimit);
    void    (WINAPI *ClearStoredMessages)(ID3D11InfoQueue *self);
    HRESULT (WINAPI *GetMessage)(ID3D11InfoQueue *self, UINT64 MessageIndex, D3D11_MESSAGE *pMessage, SIZE_T *pMessageByteLength);
    UINT64  (WINAPI *GetNumMessagesAllowedByStorageFilter)(ID3D11InfoQueue *self);
    UINT64  (WINAPI *GetNumMessagesDeniedByStorageFilter)(ID3D11InfoQueue *self);
    UINT64  (WINAPI *GetNumStoredMessages)(ID3D11InfoQueue *self);
} ID3D11InfoQueueVtbl;
struct ID3D11InfoQueue { ID3D11InfoQueueVtbl *lpVtbl; };

#endif /* _D3D11_H */
