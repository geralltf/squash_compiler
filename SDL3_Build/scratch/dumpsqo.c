/* Throwaway tool: read a .sqo and print "offset name" sorted by offset,
 * for mapping a crash/hang RIP back to the enclosing squash-compiled
 * function. Not part of the build; compiled ad hoc when debugging. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../objfile.h"

static int cmp(const void *a, const void *b) {
    const int *ia = *(int**)a, *ib = *(int**)b;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: dumpsqo file.sqo\n"); return 1; }
    ObjFile obj;
    if (!objfile_read(argv[1], &obj)) { fprintf(stderr, "read failed\n"); return 1; }

    int n = obj.export_count;
    int *idx = malloc(sizeof(int) * n);
    for (int i = 0; i < n; i++) idx[i] = i;
    /* simple insertion sort by offset */
    for (int i = 1; i < n; i++) {
        int key = idx[i]; int j = i - 1;
        while (j >= 0 && obj.export_offsets[idx[j]] > obj.export_offsets[key]) {
            idx[j+1] = idx[j]; j--;
        }
        idx[j+1] = key;
    }
    for (int i = 0; i < n; i++) {
        printf("%d %s\n", obj.export_offsets[idx[i]], obj.export_names[idx[i]]);
    }
    return 0;
}
