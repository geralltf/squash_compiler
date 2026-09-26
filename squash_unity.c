/* Self-hosting unity build: #includes every one of squash's own
 * implementation files into a single translation unit, then compiler.c
 * itself (which provides main()) last, instead of compiling each to its
 * own .sqo and linking them (what Makefile.squash does). Necessary, not
 * cosmetic: linking multiple separately-compiled .sqo files together hits
 * a real, documented squash bug in cross-object linking for larger
 * functions (SQW/SQS's own single-TU #include convention exists for the
 * exact same reason -- see e.g. SQW/sqw_main.c's own top comment).
 * Confirmed via a real self-hosting test: a squash binary built by
 * linking 16 separately-compiled .sqo files segfaulted on literally
 * "int main(){return 0;}" -- not a subtle input-dependent bug, the
 * multi-object link itself was broken. This file exists purely to give
 * self-hosting verification (see tools/self_verify.sh) a working
 * generation-1+ binary to test against; it changes nothing about how a
 * normal gcc build of squash itself works (Makefile.linux is untouched).
 * Order matters only in that compiler.c (main()) must come last -- every
 * other file's own header already declares what it needs from the
 * others, and every header has a real include guard, so re-#include is
 * harmless. */
#include "assembler.c"
#include "ast.c"
#include "codegen.c"
#include "arm64_asm.c"
#include "codegen_arm64.c"
#include "lexer.c"
#include "parser_new4.c"
#include "pe_builder.c"
#include "elf_builder.c"
#include "macho_builder.c"
#include "symtable.c"
#include "linker.c"
#include "winlinker.c"
#include "objfile.c"
#include "implib.c"
#include "diag.c"
#include "CS/cs_ast.c"
#include "CS/cs_lexer.c"
#include "CS/cs_parser.c"
#include "CS/cs_lower.c"
#include "CPP/cpp_ast.c"
#include "CPP/cpp_lexer.c"
#include "CPP/cpp_parser.c"
#include "CPP/cpp_lower.c"
#include "android/android_sha256.c"
#include "android/android_crc32.c"
#include "android/android_zip.c"
#include "android/android_axml.c"
#include "android/android_manifest.c"
#include "android/android_bignum.c"
#include "android/android_der.c"
#include "android/android_rsa.c"
#include "android/android_rsa_keyfile.c"
#include "android/android_x509.c"
#include "android/android_apk_digest.c"
#include "android/android_apk_sign.c"
#include "android/android_base64.c"
#include "android/android_apk_sign_v1.c"
#include "android/android_pack.c"
#include "android/android_sha1.c"
#include "android/android_adler32.c"
#include "android/android_dex.c"
#include "compiler.c"
