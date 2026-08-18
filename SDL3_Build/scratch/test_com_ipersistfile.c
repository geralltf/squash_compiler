/* Further real-COM stress test: QueryInterface for a SECOND real interface
 * (IPersistFile, which IShellLinkW objects also implement) and call
 * through ITS vtable (a different shape: IUnknown's 3 methods + IPersist's
 * GetClassID + IPersistFile's IsDirty/Load/Save/SaveCompleted/GetCurFile).
 * Exercises multi-interface QueryInterface and a deeper vtable than the
 * base test_com_real_object.c. */
#include <windows.h>
#include <shobjidl.h>
#include <stdio.h>

const GUID MY_CLSID_ShellLink   = {0x00021401,0x0000,0x0000,{0xC0,0,0,0,0,0,0,0x46}};
const GUID MY_IID_IShellLinkW   = {0x000214F9,0x0000,0x0000,{0xC0,0,0,0,0,0,0,0x46}};
const GUID MY_IID_IPersistFile  = {0x0000010b,0x0000,0x0000,{0xC0,0,0,0,0,0,0,0x46}};

int main(void) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    printf("CoInitializeEx hr=0x%08lx\n", (unsigned long)hr);

    IShellLinkW *link = NULL;
    hr = CoCreateInstance(&MY_CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                           &MY_IID_IShellLinkW, (void**)&link);
    printf("CoCreateInstance hr=0x%08lx link=%p\n", (unsigned long)hr, (void*)link);

    if (link) {
        link->lpVtbl->SetPath(link, L"C:\\Windows\\notepad.exe");

        IPersistFile *pf = NULL;
        hr = link->lpVtbl->QueryInterface(link, &MY_IID_IPersistFile, (void**)&pf);
        printf("QueryInterface(IPersistFile) hr=0x%08lx pf=%p\n", (unsigned long)hr, (void*)pf);

        if (pf) {
            BOOL dirty = pf->lpVtbl->IsDirty(pf);
            printf("IsDirty=%d\n", (int)dirty);

            LPOLESTR curfile = NULL;
            hr = pf->lpVtbl->GetCurFile(pf, &curfile);
            printf("GetCurFile hr=0x%08lx curfile=%ls\n", (unsigned long)hr,
                   curfile ? curfile : L"(null)");
            if (curfile) CoTaskMemFree(curfile);

            pf->lpVtbl->Release(pf);
        }

        link->lpVtbl->Release(link);
    }

    CoUninitialize();
    printf("done\n");
    return 0;
}
