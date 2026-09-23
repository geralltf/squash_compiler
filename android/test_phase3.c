/* Standalone structural test for the Android ET_DYN .so codegen path added
 * to elf_builder.c. Bypasses the full squash compiler frontend (no
 * lexer/parser/codegen involved) and builds an ELFBuildInput by hand, the
 * same way codegen_arm64.c's real integration eventually will, so the ELF
 * writer itself can be verified before wiring up the rest of the pipeline. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../elf_builder.h"
#include "../ast.h"

int main(void) {
    g_squash_libc_soname = "libc.so"; /* bionic soname: no ".so.N" suffix */

    /* Function body: just `ret` (0xD65F03C0). Enough to validate ELF/
     * dynsym/hash structure without needing real NativeActivity glue yet. */
    uint8_t text[4] = { 0xC0, 0x03, 0x5F, 0xD6 };

    Relocation relocs[1];
    relocs[0].offset = 0;
    relocs[0].kind = RELOC_A64_IAT_ADRP; /* irrelevant: __entry__ is special-cased */
    relocs[0].symbol = (char *)"__entry__";
    relocs[0].addend = 0; /* exported function starts at text offset 0 */

    char *import_specs[1];
    char import_buf[128];
    snprintf(import_buf, sizeof import_buf, "%s:write", g_squash_libc_soname);
    import_specs[0] = import_buf;

    ELFBuildInput in;
    memset(&in, 0, sizeof in);
    in.is_64bit = 1;
    in.is_arm64 = 1;
    in.is_android = 1;
    in.android_soname = "libapp.so";
    in.android_export_name = "ANativeActivity_onCreate";
    in.text = text;
    in.text_len = sizeof text;
    in.relocs = relocs;
    in.reloc_count = 1;
    in.import_specs = import_specs;
    in.import_count = 1;
    in.entry_func = (char *)"ANativeActivity_onCreate";
    in.output_path = "/tmp/claude-1000/-home-squash-projects-squash-compler-squash-compiler/3a439974-83aa-407c-b935-d7a0d8fca460/scratchpad/libapp.so";

    int rc = elf_link_and_write(&in);
    printf("elf_link_and_write returned %d\n", rc);
    return rc;
}
