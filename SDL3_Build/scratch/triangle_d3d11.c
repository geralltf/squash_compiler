#include <windows.h>
#include <dxgi.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>

/* Animated, per-vertex-color-interpolated triangle in real Direct3D 11.
 * Window + device/swapchain creation, runtime HLSL->bytecode compilation
 * via the real d3dcompiler_47.dll (no precompiled shader blobs shipped),
 * vertex/pixel shaders, input layout, vertex buffer, and a per-frame
 * constant buffer update driving a 2D rotation -- exercises squash's COM
 * vtable dispatch against the genuine system d3d11.dll/dxgi.dll, not a
 * fake implementation. */

/* Real GUID for ID3D11Texture2D (needed by IDXGISwapChain::GetBuffer's
 * riid parameter to retrieve the swap chain's back buffer as a texture). */
static const GUID IID_ID3D11Texture2D = {0x6f15aaf2, 0xd208, 0x4e89, {0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c}};

typedef struct { float angle; float pad0, pad1, pad2; } ConstantBufferData;

static const char *g_vs_src =
    "cbuffer ConstantBuffer : register(b0) { float angle; float3 padding; };\n"
    "struct VSInput { float2 pos : POSITION; float3 color : COLOR; };\n"
    "struct PSInput { float4 pos : SV_POSITION; float3 color : COLOR; };\n"
    "PSInput VSMain(VSInput input) {\n"
    "    PSInput output;\n"
    "    float s = sin(angle);\n"
    "    float c = cos(angle);\n"
    "    float2 rotated;\n"
    "    rotated.x = input.pos.x * c - input.pos.y * s;\n"
    "    rotated.y = input.pos.x * s + input.pos.y * c;\n"
    "    output.pos = float4(rotated, 0.0, 1.0);\n"
    "    output.color = input.color;\n"
    "    return output;\n"
    "}\n";

static const char *g_ps_src =
    "struct PSInput { float4 pos : SV_POSITION; float3 color : COLOR; };\n"
    "float4 PSMain(PSInput input) : SV_TARGET {\n"
    "    return float4(input.color, 1.0);\n"
    "}\n";

typedef struct { float x, y; float r, g, b; } Vertex;

static HWND g_hwnd;
static int g_running = 1;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CLOSE || msg == WM_DESTROY) {
        g_running = 0;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static ID3DBlob *compile_shader(const char *src, const char *entry, const char *target) {
    ID3DBlob *code = NULL;
    ID3DBlob *errors = NULL;
    HRESULT hr = D3DCompile(src, strlen(src), NULL, NULL, NULL, entry, target,
                             D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
    if (FAILED(hr)) {
        printf("D3DCompile(%s) failed hr=0x%08lx\n", entry, (unsigned long)hr); fflush(stdout);
        if (errors) {
            printf("  errors: %s\n", (char *)errors->lpVtbl->GetBufferPointer(errors)); fflush(stdout);
            errors->lpVtbl->Release(errors);
        }
        return NULL;
    }
    if (errors) errors->lpVtbl->Release(errors);
    return code;
}

int main(void) {
    printf("triangle_d3d11: start\n"); fflush(stdout);

    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "SquashD3D11TriangleClass";
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)32512); /* IDC_ARROW */
    RegisterClassExA(&wc);

    RECT wr = {0, 0, 800, 600};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowExA(0, "SquashD3D11TriangleClass", "squash D3D11 triangle (animated)",
                              WS_OVERLAPPEDWINDOW, 100, 100, wr.right - wr.left, wr.bottom - wr.top,
                              NULL, NULL, wc.hInstance, NULL);
    if (!g_hwnd) {
        printf("CreateWindowExA failed\n"); fflush(stdout);
        return 1;
    }
    ShowWindow(g_hwnd, 1 /* SW_SHOWNORMAL */);
    /* Force topmost + try to take foreground: this demo is often launched
     * non-interactively (e.g. from a script), in which case the new window
     * has no guarantee of being on top of or above whatever else is on
     * screen -- without this, a later pixel-verification read from the
     * screen DC could see an occluding window instead of this one. */
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(g_hwnd);
    printf("main: window created\n"); fflush(stdout);

    DXGI_SWAP_CHAIN_DESC scd;
    memset(&scd, 0, sizeof(scd));
    scd.BufferDesc.Width = 800;
    scd.BufferDesc.Height = 600;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 1;
    scd.OutputWindow = g_hwnd;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL wantLevels[4] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3 };
    D3D_FEATURE_LEVEL gotLevel;
    IDXGISwapChain *swapChain = NULL;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *ctx = NULL;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
        wantLevels, 4, D3D11_SDK_VERSION, &scd, &swapChain, &device, &gotLevel, &ctx);
    if (FAILED(hr)) {
        printf("D3D11CreateDeviceAndSwapChain failed hr=0x%08lx\n", (unsigned long)hr); fflush(stdout);
        return 1;
    }
    printf("main: device+swapchain created, feature level=0x%x\n", (unsigned)gotLevel); fflush(stdout);

    ID3D11Texture2D *backBuffer = NULL;
    hr = swapChain->lpVtbl->GetBuffer(swapChain, 0, &IID_ID3D11Texture2D, (void **)&backBuffer);
    if (FAILED(hr)) {
        printf("GetBuffer failed hr=0x%08lx\n", (unsigned long)hr); fflush(stdout);
        return 1;
    }
    ID3D11RenderTargetView *rtv = NULL;
    hr = device->lpVtbl->CreateRenderTargetView(device, (ID3D11Resource *)backBuffer, NULL, &rtv);
    backBuffer->lpVtbl->Release(backBuffer);
    if (FAILED(hr)) {
        printf("CreateRenderTargetView failed hr=0x%08lx\n", (unsigned long)hr); fflush(stdout);
        return 1;
    }
    printf("main: render target view created\n"); fflush(stdout);

    ID3DBlob *vsBlob = compile_shader(g_vs_src, "VSMain", "vs_5_0");
    ID3DBlob *psBlob = compile_shader(g_ps_src, "PSMain", "ps_5_0");
    if (!vsBlob || !psBlob) {
        printf("main: shader compile failed\n"); fflush(stdout);
        return 1;
    }
    printf("main: shaders compiled (vs=%zu bytes, ps=%zu bytes)\n",
           (size_t)vsBlob->lpVtbl->GetBufferSize(vsBlob), (size_t)psBlob->lpVtbl->GetBufferSize(psBlob)); fflush(stdout);

    ID3D11VertexShader *vs = NULL;
    ID3D11PixelShader *ps = NULL;
    device->lpVtbl->CreateVertexShader(device, vsBlob->lpVtbl->GetBufferPointer(vsBlob),
        vsBlob->lpVtbl->GetBufferSize(vsBlob), NULL, &vs);
    device->lpVtbl->CreatePixelShader(device, psBlob->lpVtbl->GetBufferPointer(psBlob),
        psBlob->lpVtbl->GetBufferSize(psBlob), NULL, &ps);

    D3D11_INPUT_ELEMENT_DESC layoutDesc[2];
    memset(layoutDesc, 0, sizeof(layoutDesc));
    layoutDesc[0].SemanticName = "POSITION";
    layoutDesc[0].Format = DXGI_FORMAT_R32G32_FLOAT;
    layoutDesc[0].AlignedByteOffset = 0;
    layoutDesc[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    layoutDesc[1].SemanticName = "COLOR";
    layoutDesc[1].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    layoutDesc[1].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
    layoutDesc[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;

    ID3D11InputLayout *inputLayout = NULL;
    hr = device->lpVtbl->CreateInputLayout(device, layoutDesc, 2,
        vsBlob->lpVtbl->GetBufferPointer(vsBlob), vsBlob->lpVtbl->GetBufferSize(vsBlob), &inputLayout);
    if (FAILED(hr)) {
        printf("CreateInputLayout failed hr=0x%08lx\n", (unsigned long)hr); fflush(stdout);
        return 1;
    }
    printf("main: input layout created\n"); fflush(stdout);

    Vertex verts[3] = {
        {  0.0f,  0.5f, 1.0f, 0.0f, 0.0f },
        {  0.5f, -0.5f, 0.0f, 1.0f, 0.0f },
        { -0.5f, -0.5f, 0.0f, 0.0f, 1.0f },
    };
    D3D11_BUFFER_DESC vbDesc;
    memset(&vbDesc, 0, sizeof(vbDesc));
    vbDesc.ByteWidth = sizeof(verts);
    vbDesc.Usage = D3D11_USAGE_DEFAULT;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData;
    memset(&vbData, 0, sizeof(vbData));
    vbData.pSysMem = verts;
    ID3D11Buffer *vertexBuffer = NULL;
    hr = device->lpVtbl->CreateBuffer(device, &vbDesc, &vbData, &vertexBuffer);
    if (FAILED(hr)) {
        printf("CreateBuffer(vertex) failed hr=0x%08lx\n", (unsigned long)hr); fflush(stdout);
        return 1;
    }

    D3D11_BUFFER_DESC cbDesc;
    memset(&cbDesc, 0, sizeof(cbDesc));
    cbDesc.ByteWidth = sizeof(ConstantBufferData);
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ID3D11Buffer *constantBuffer = NULL;
    hr = device->lpVtbl->CreateBuffer(device, &cbDesc, NULL, &constantBuffer);
    if (FAILED(hr)) {
        printf("CreateBuffer(constant) failed hr=0x%08lx\n", (unsigned long)hr); fflush(stdout);
        return 1;
    }
    printf("main: vertex+constant buffers created\n"); fflush(stdout);

    /* D3D11's DEFAULT rasterizer state (used whenever none is explicitly
     * bound) back-face-culls with CullMode=BACK, FrontCounterClockwise=
     * FALSE (clockwise-in-screen-space = front). NDC is Y-up but the
     * viewport transform flips to screen-space Y-down, so a triangle
     * wound one way in NDC comes out the other way in screen space --
     * this demo's vertex order was, in fact, back-facing under that
     * default, so the triangle was being silently culled every frame
     * (clear color only, no visible geometry). CullMode=NONE sidesteps
     * needing to reason about winding at all. */
    D3D11_RASTERIZER_DESC rsDesc;
    memset(&rsDesc, 0, sizeof(rsDesc));
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE;
    rsDesc.FrontCounterClockwise = FALSE;
    rsDesc.DepthClipEnable = TRUE;
    ID3D11RasterizerState *rasterizerState = NULL;
    hr = device->lpVtbl->CreateRasterizerState(device, &rsDesc, (void **)&rasterizerState);
    if (FAILED(hr)) {
        printf("CreateRasterizerState failed hr=0x%08lx\n", (unsigned long)hr); fflush(stdout);
        return 1;
    }
    printf("main: rasterizer state created (cull disabled)\n"); fflush(stdout);

    D3D11_VIEWPORT vp;
    vp.TopLeftX = 0; vp.TopLeftY = 0;
    vp.Width = 800; vp.Height = 600;
    vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;

    UINT stride = sizeof(Vertex);
    UINT offset = 0;

    LARGE_INTEGER freq, startTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&startTime);

    int frame_count = 0;
    const int max_frames = 300; /* run a fixed, bounded number of frames then exit cleanly */

    printf("main: entering render loop (%d frames)\n", max_frames); fflush(stdout);
    while (g_running && frame_count < max_frames) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, 1 /* PM_REMOVE */)) {
            if (msg.message == WM_QUIT) { g_running = 0; break; }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!g_running) break;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double elapsed = (double)(now.QuadPart - startTime.QuadPart) / (double)freq.QuadPart;

        ConstantBufferData cbData;
        cbData.angle = (float)(elapsed * 1.5 /* rad/s */);
        cbData.pad0 = cbData.pad1 = cbData.pad2 = 0.0f;
        ctx->lpVtbl->UpdateSubresource(ctx, (ID3D11Resource *)constantBuffer, 0, NULL, &cbData, 0, 0);

        float clearColor[4] = { 0.05f, 0.05f, 0.1f, 1.0f };
        ctx->lpVtbl->OMSetRenderTargets(ctx, 1, &rtv, NULL);
        ctx->lpVtbl->ClearRenderTargetView(ctx, rtv, clearColor);
        ctx->lpVtbl->RSSetViewports(ctx, 1, &vp);
        ctx->lpVtbl->RSSetState(ctx, (void *)rasterizerState);
        ctx->lpVtbl->IASetInputLayout(ctx, inputLayout);
        ctx->lpVtbl->IASetVertexBuffers(ctx, 0, 1, &vertexBuffer, &stride, &offset);
        ctx->lpVtbl->IASetPrimitiveTopology(ctx, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->lpVtbl->VSSetShader(ctx, vs, NULL, 0);
        ctx->lpVtbl->VSSetConstantBuffers(ctx, 0, 1, &constantBuffer);
        ctx->lpVtbl->PSSetShader(ctx, ps, NULL, 0);
        ctx->lpVtbl->Draw(ctx, 3, 0);

        swapChain->lpVtbl->Present(swapChain, 1, 0);

        frame_count++;
        if (frame_count % 60 == 0) printf("main: frame=%d elapsed=%.2fs angle=%.2f\n", frame_count, elapsed, cbData.angle); fflush(stdout);
    }

    printf("main: render loop finished, frame_count=%d\n", frame_count); fflush(stdout);

    rasterizerState->lpVtbl->Release(rasterizerState);
    vertexBuffer->lpVtbl->Release(vertexBuffer);
    constantBuffer->lpVtbl->Release(constantBuffer);
    inputLayout->lpVtbl->Release(inputLayout);
    vs->lpVtbl->Release(vs);
    ps->lpVtbl->Release(ps);
    vsBlob->lpVtbl->Release(vsBlob);
    psBlob->lpVtbl->Release(psBlob);
    rtv->lpVtbl->Release(rtv);
    swapChain->lpVtbl->Release(swapChain);
    ctx->lpVtbl->Release(ctx);
    device->lpVtbl->Release(device);
    DestroyWindow(g_hwnd);

    printf("main: done, exiting cleanly\n"); fflush(stdout);
    return 0;
}
