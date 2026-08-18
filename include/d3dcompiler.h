#ifndef _D3DCOMPILER_H
#define _D3DCOMPILER_H
/* Minimal d3dcompiler surface: just ID3DBlob + D3DCompile, enough to
 * compile HLSL source strings to shader bytecode at runtime (avoids
 * needing precompiled .cso blobs shipped alongside the demo — the HLSL
 * source lives directly in the .c file and gets compiled by the real
 * system d3dcompiler_47.dll on first run, exactly like a real game's
 * shader hot-reload path). */
#include "include/windows.h"

/* --- ID3DBlob : IUnknown --- */
typedef struct ID3DBlob ID3DBlob;
typedef struct ID3DBlobVtbl {
    HRESULT (WINAPI *QueryInterface)(ID3DBlob *self, REFIID riid, void **ppvObject);
    ULONG   (WINAPI *AddRef)(ID3DBlob *self);
    ULONG   (WINAPI *Release)(ID3DBlob *self);
    void   *(WINAPI *GetBufferPointer)(ID3DBlob *self);
    SIZE_T  (WINAPI *GetBufferSize)(ID3DBlob *self);
} ID3DBlobVtbl;
struct ID3DBlob { ID3DBlobVtbl *lpVtbl; };

#define D3DCOMPILE_ENABLE_STRICTNESS 0x0800

/* Real exported entry point in d3dcompiler_47.dll. pDefines/pInclude left
 * as void* (always NULL at our call sites) -- the real types are
 * D3D_SHADER_MACRO pointer and ID3DInclude pointer, neither of which this
 * project needs to construct. */
HRESULT WINAPI D3DCompile(
    const void *pSrcData, SIZE_T SrcDataSize, LPCSTR pSourceName,
    const void *pDefines, void *pInclude, LPCSTR pEntrypoint, LPCSTR pTarget,
    UINT Flags1, UINT Flags2, ID3DBlob **ppCode, ID3DBlob **ppErrorMsgs);

#endif /* _D3DCOMPILER_H */
