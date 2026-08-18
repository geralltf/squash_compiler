/* Real end-to-end COM stack test: CoCreateInstance() against an ACTUAL
 * system-provided COM server (the Shell Link object), not a hand-written
 * vtable. Exercises: real .lib-resolved CoInitializeEx/CoCreateInstance/
 * CoUninitialize calls, real GUID literal data, and real vtable method
 * dispatch (QueryInterface/Release) through a COM server actually
 * implemented by shell32.dll.
 *
 * NOT using DEFINE_GUID+INITGUID here: that macro combo (with #define
 * INITGUID before #include <windows.h>) turns every one of the SDK's own
 * thousands of GUID declarations into real initialized globals across the
 * whole header set, which hits a known, documented, separate scale issue
 * (symtable's global scope is a linked list -> O(n^2) -> hangs) — see
 * project memory. Defining just the 3 GUIDs actually needed, as plain
 * const-struct globals, sidesteps that entirely. */
#include <windows.h>
#include <shobjidl.h>
#include <stdio.h>

const GUID MY_CLSID_ShellLink = {0x00021401,0x0000,0x0000,{0xC0,0,0,0,0,0,0,0x46}};
const GUID MY_IID_IShellLinkW = {0x000214F9,0x0000,0x0000,{0xC0,0,0,0,0,0,0,0x46}};
const GUID MY_IID_IUnknown    = {0x00000000,0x0000,0x0000,{0xC0,0,0,0,0,0,0,0x46}};

int main(void) {
    /* NOTE: no fflush() calls here — this test is compiled against the
     * REAL Microsoft SDK's own stdio.h (from the scratch include/ dir),
     * not squash's own minimal shim, so squash's fflush-sentinel-crash fix
     * (see project memory) doesn't apply here; fflush(stdout/stderr)
     * would genuinely segfault exactly as documented. */
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    printf("CoInitializeEx hr=0x%08lx\n", (unsigned long)hr);

    IShellLinkW *link = NULL;
    hr = CoCreateInstance(&MY_CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                           &MY_IID_IShellLinkW, (void**)&link);
    printf("CoCreateInstance hr=0x%08lx link=%p\n", (unsigned long)hr, (void*)link);

    if (link) {
        /* Real vtable dispatch through the system's own shell32.dll
         * implementation - QueryInterface for IUnknown must succeed on any
         * real COM object. */
        void *unk = NULL;
        HRESULT qi = link->lpVtbl->QueryInterface(link, &MY_IID_IUnknown, &unk);
        printf("QueryInterface(IUnknown) hr=0x%08lx unk=%p\n", (unsigned long)qi, unk);
        if (unk) {
            ((IUnknown*)unk)->lpVtbl->Release((IUnknown*)unk);
        }

        WCHAR path[MAX_PATH];
        hr = link->lpVtbl->SetPath(link, L"C:\\Windows\\notepad.exe");
        printf("SetPath hr=0x%08lx\n", (unsigned long)hr);
        hr = link->lpVtbl->GetPath(link, path, MAX_PATH, NULL, 0);
        printf("GetPath hr=0x%08lx path=%ls\n", (unsigned long)hr, path);

        link->lpVtbl->Release(link);
    }

    CoUninitialize();
    printf("done\n");
    return 0;
}
