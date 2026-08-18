#ifndef _DXGI_H
#define _DXGI_H
/* Minimal DXGI surface — just enough of the real dxgi.h to create a swap
 * chain for a Direct3D 11 window and present frames. Not a full port (no
 * IDXGIFactory1/2/adapters/outputs enumeration surface) — D3D11CreateDevice
 * AndSwapChain (see d3d11.h) covers factory creation implicitly, which is
 * all a minimal single-window app needs. Hand-written to match the real
 * ABI (struct layout / vtable order) since this calls into the actual
 * system dxgi.dll — not a fake/stub implementation. */
#include "include/windows.h"

typedef enum DXGI_FORMAT {
    DXGI_FORMAT_UNKNOWN = 0,
    DXGI_FORMAT_R32G32B32A32_FLOAT = 2,
    DXGI_FORMAT_R32G32B32_FLOAT = 6,
    DXGI_FORMAT_R32G32_FLOAT = 16,
    DXGI_FORMAT_R8G8B8A8_UNORM = 28,
    DXGI_FORMAT_R16_UINT = 57,
    DXGI_FORMAT_R32_UINT = 42
} DXGI_FORMAT;

typedef struct DXGI_RATIONAL {
    UINT Numerator;
    UINT Denominator;
} DXGI_RATIONAL;

typedef struct DXGI_MODE_DESC {
    UINT Width;
    UINT Height;
    DXGI_RATIONAL RefreshRate;
    DXGI_FORMAT Format;
    UINT ScanlineOrdering; /* DXGI_MODE_SCANLINE_ORDER, 0=unspecified */
    UINT Scaling;          /* DXGI_MODE_SCALING, 0=unspecified */
} DXGI_MODE_DESC;

typedef struct DXGI_SAMPLE_DESC {
    UINT Count;
    UINT Quality;
} DXGI_SAMPLE_DESC;

#define DXGI_USAGE_RENDER_TARGET_OUTPUT 0x00000020UL
#define DXGI_SWAP_EFFECT_DISCARD 0

typedef struct DXGI_SWAP_CHAIN_DESC {
    DXGI_MODE_DESC BufferDesc;
    DXGI_SAMPLE_DESC SampleDesc;
    DWORD BufferUsage;
    UINT BufferCount;
    HWND OutputWindow;
    BOOL Windowed;
    UINT SwapEffect;
    UINT Flags;
} DXGI_SWAP_CHAIN_DESC;

/* --- IDXGIObject : IUnknown --- */
typedef struct IDXGIObject IDXGIObject;
typedef struct IDXGIObjectVtbl {
    HRESULT (WINAPI *QueryInterface)(IDXGIObject *self, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IDXGIObject *self);
    ULONG   (WINAPI *Release)(IDXGIObject *self);
    HRESULT (WINAPI *SetPrivateData)(IDXGIObject *self, REFGUID Name, UINT DataSize, const void *pData);
    HRESULT (WINAPI *SetPrivateDataInterface)(IDXGIObject *self, REFGUID Name, const IUnknown *pUnknown);
    HRESULT (WINAPI *GetPrivateData)(IDXGIObject *self, REFGUID Name, UINT *pDataSize, void *pData);
    HRESULT (WINAPI *GetParent)(IDXGIObject *self, REFIID riid, void **ppParent);
} IDXGIObjectVtbl;
struct IDXGIObject { IDXGIObjectVtbl *lpVtbl; };

/* --- IDXGIDeviceSubObject : IDXGIObject --- */
typedef struct IDXGIDeviceSubObject IDXGIDeviceSubObject;
typedef struct IDXGIDeviceSubObjectVtbl {
    HRESULT (WINAPI *QueryInterface)(IDXGIDeviceSubObject *self, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IDXGIDeviceSubObject *self);
    ULONG   (WINAPI *Release)(IDXGIDeviceSubObject *self);
    HRESULT (WINAPI *SetPrivateData)(IDXGIDeviceSubObject *self, REFGUID Name, UINT DataSize, const void *pData);
    HRESULT (WINAPI *SetPrivateDataInterface)(IDXGIDeviceSubObject *self, REFGUID Name, const IUnknown *pUnknown);
    HRESULT (WINAPI *GetPrivateData)(IDXGIDeviceSubObject *self, REFGUID Name, UINT *pDataSize, void *pData);
    HRESULT (WINAPI *GetParent)(IDXGIDeviceSubObject *self, REFIID riid, void **ppParent);
    HRESULT (WINAPI *GetDevice)(IDXGIDeviceSubObject *self, REFIID riid, void **ppDevice);
} IDXGIDeviceSubObjectVtbl;
struct IDXGIDeviceSubObject { IDXGIDeviceSubObjectVtbl *lpVtbl; };

/* --- IDXGISwapChain : IDXGIDeviceSubObject --- (only through GetBuffer;
 * everything after that in the real interface — SetFullscreenState,
 * GetDesc, ResizeBuffers, etc — is never called by this minimal demo, so
 * it's simply not declared. Omitting trailing, unused slots is safe: it
 * only affects how much of the struct we can see, not the real binary
 * layout of the object dxgi.dll actually implements.) */
typedef struct IDXGISwapChain IDXGISwapChain;
typedef struct IDXGISwapChainVtbl {
    HRESULT (WINAPI *QueryInterface)(IDXGISwapChain *self, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(IDXGISwapChain *self);
    ULONG   (WINAPI *Release)(IDXGISwapChain *self);
    HRESULT (WINAPI *SetPrivateData)(IDXGISwapChain *self, REFGUID Name, UINT DataSize, const void *pData);
    HRESULT (WINAPI *SetPrivateDataInterface)(IDXGISwapChain *self, REFGUID Name, const IUnknown *pUnknown);
    HRESULT (WINAPI *GetPrivateData)(IDXGISwapChain *self, REFGUID Name, UINT *pDataSize, void *pData);
    HRESULT (WINAPI *GetParent)(IDXGISwapChain *self, REFIID riid, void **ppParent);
    HRESULT (WINAPI *GetDevice)(IDXGISwapChain *self, REFIID riid, void **ppDevice);
    HRESULT (WINAPI *Present)(IDXGISwapChain *self, UINT SyncInterval, UINT Flags);
    HRESULT (WINAPI *GetBuffer)(IDXGISwapChain *self, UINT Buffer, REFIID riid, void **ppSurface);
} IDXGISwapChainVtbl;
struct IDXGISwapChain { IDXGISwapChainVtbl *lpVtbl; };

#endif /* _DXGI_H */
