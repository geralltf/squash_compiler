/* Binary XML (AXML) writer. See android_axml.h for format references.
 *
 * Layout of the document we emit:
 *   ResChunk_header (RES_XML_TYPE)
 *     ResStringPool_header + offsets + UTF-16LE string data      (chunk)
 *     ResXMLTree_resourceMap (RES_XML_RESOURCE_MAP_TYPE)          (chunk)
 *     RES_XML_START_NAMESPACE_TYPE node
 *       RES_XML_START_ELEMENT_TYPE node (root)
 *         ... recursively, children ...
 *       RES_XML_END_ELEMENT_TYPE node (root)
 *     RES_XML_END_NAMESPACE_TYPE node
 *
 * The resource map covers exactly the leading N strings of the pool, where
 * those N strings are every attribute name that carries a known android:*
 * resource id. All other strings (element names, unprefixed attribute
 * names, string attribute values, the namespace prefix/uri) are appended
 * after that prefix, outside the map's coverage -- which is valid per spec
 * since indices beyond the map length are simply treated as "no resource
 * id" by the parser. */
#include "android_axml.h"
#include <stdlib.h>
#include <string.h>

/* ---- chunk type constants ---- */
#define RES_STRING_POOL_TYPE        0x0001
#define RES_XML_TYPE                0x0003
#define RES_XML_START_NAMESPACE_TYPE 0x0100
#define RES_XML_END_NAMESPACE_TYPE   0x0101
#define RES_XML_START_ELEMENT_TYPE   0x0102
#define RES_XML_END_ELEMENT_TYPE     0x0103
#define RES_XML_RESOURCE_MAP_TYPE    0x0180

#define TYPE_STRING       0x03
#define TYPE_INT_DEC      0x10
#define TYPE_INT_HEX      0x11
#define TYPE_INT_BOOLEAN  0x12

#define NO_REF 0xFFFFFFFFu

/* ---------------------------------------------------------------- bytebuf */
typedef struct {
    unsigned char *data;
    size_t len, cap;
} bytebuf;

static void bb_init(bytebuf *b) { b->data = NULL; b->len = 0; b->cap = 0; }
static void bb_free(bytebuf *b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }

static void bb_reserve(bytebuf *b, size_t extra) {
    if (b->len + extra <= b->cap) return;
    {
        size_t newcap = b->cap ? b->cap * 2 : 256;
        while (newcap < b->len + extra) newcap *= 2;
        b->data = (unsigned char *)realloc(b->data, newcap);
        b->cap = newcap;
    }
}
static void bb_append(bytebuf *b, const void *p, size_t n) {
    bb_reserve(b, n);
    memcpy(b->data + b->len, p, n);
    b->len += n;
}
static void bb_u16(bytebuf *b, uint16_t v) {
    unsigned char le[2]; le[0]=(unsigned char)v; le[1]=(unsigned char)(v>>8);
    bb_append(b, le, 2);
}
static void bb_u32(bytebuf *b, uint32_t v) {
    unsigned char le[4];
    le[0]=(unsigned char)v; le[1]=(unsigned char)(v>>8);
    le[2]=(unsigned char)(v>>16); le[3]=(unsigned char)(v>>24);
    bb_append(b, le, 4);
}
static void bb_pad4(bytebuf *b) {
    while (b->len % 4 != 0) { unsigned char z = 0; bb_append(b, &z, 1); }
}

/* ------------------------------------------------------------- tree model */
typedef struct {
    char *ns_uri;   /* NULL if unprefixed */
    char *name;
    uint32_t res_id;
    axml_attr_type type;
    char *str_value;   /* for AXML_ATTR_STRING */
    int32_t int_value; /* for the int/bool types */
} axml_attr;

struct axml_node {
    char *ns_uri; /* NULL if unprefixed */
    char *name;
    axml_attr *attrs;
    size_t n_attrs, cap_attrs;
    axml_node **children;
    size_t n_children, cap_children;
};

static char *dup_or_null(const char *s) { return s ? strdup(s) : NULL; }

axml_node *axml_new_element(const char *ns_uri, const char *name) {
    axml_node *n = (axml_node *)calloc(1, sizeof(*n));
    n->ns_uri = dup_or_null(ns_uri);
    n->name = strdup(name);
    return n;
}

void axml_free(axml_node *n) {
    size_t i;
    if (!n) return;
    for (i = 0; i < n->n_attrs; i++) {
        free(n->attrs[i].ns_uri);
        free(n->attrs[i].name);
        free(n->attrs[i].str_value);
    }
    free(n->attrs);
    for (i = 0; i < n->n_children; i++) axml_free(n->children[i]);
    free(n->children);
    free(n->ns_uri);
    free(n->name);
    free(n);
}

void axml_add_child(axml_node *parent, axml_node *child) {
    if (parent->n_children == parent->cap_children) {
        size_t nc = parent->cap_children ? parent->cap_children * 2 : 4;
        parent->children = (axml_node **)realloc(parent->children, nc * sizeof(axml_node *));
        parent->cap_children = nc;
    }
    parent->children[parent->n_children++] = child;
}

static axml_attr *push_attr(axml_node *n) {
    if (n->n_attrs == n->cap_attrs) {
        size_t nc = n->cap_attrs ? n->cap_attrs * 2 : 4;
        n->attrs = (axml_attr *)realloc(n->attrs, nc * sizeof(axml_attr));
        n->cap_attrs = nc;
    }
    memset(&n->attrs[n->n_attrs], 0, sizeof(axml_attr));
    return &n->attrs[n->n_attrs++];
}

void axml_add_attr_string(axml_node *n, const char *ns_uri, const char *name,
                           uint32_t res_id, const char *value) {
    axml_attr *a = push_attr(n);
    a->ns_uri = dup_or_null(ns_uri);
    a->name = strdup(name);
    a->res_id = res_id;
    a->type = AXML_ATTR_STRING;
    a->str_value = strdup(value);
}

void axml_add_attr_int(axml_node *n, const char *ns_uri, const char *name,
                        uint32_t res_id, int32_t value, axml_attr_type type) {
    axml_attr *a = push_attr(n);
    a->ns_uri = dup_or_null(ns_uri);
    a->name = strdup(name);
    a->res_id = res_id;
    a->type = type;
    a->int_value = value;
}

/* --------------------------------------------------------------- strings */
/* Dedup'd string table. res_ids[i] is nonzero only for entries added
 * during the "resid attribute names" pass, and only entries added during
 * that pass may end up inside the resource map's coverage. */
typedef struct {
    char **strs;
    uint32_t *res_ids;
    size_t n, cap;
} strtab;

static void strtab_init(strtab *t) { t->strs = NULL; t->res_ids = NULL; t->n = 0; t->cap = 0; }

static uint32_t strtab_intern(strtab *t, const char *s, uint32_t res_id) {
    size_t i;
    for (i = 0; i < t->n; i++) {
        if (strcmp(t->strs[i], s) == 0) return (uint32_t)i;
    }
    if (t->n == t->cap) {
        size_t nc = t->cap ? t->cap * 2 : 16;
        t->strs = (char **)realloc(t->strs, nc * sizeof(char *));
        t->res_ids = (uint32_t *)realloc(t->res_ids, nc * sizeof(uint32_t));
        t->cap = nc;
    }
    t->strs[t->n] = strdup(s);
    t->res_ids[t->n] = res_id;
    return (uint32_t)(t->n++);
}

/* Pass 1: collect every attribute name (across the whole tree) that has a
 * known resource id, in first-seen order, so they land at the very front
 * of the pool (this is what the resource map will cover). */
static void collect_resid_attr_names(axml_node *n, strtab *t) {
    size_t i;
    for (i = 0; i < n->n_attrs; i++) {
        if (n->attrs[i].ns_uri && n->attrs[i].res_id != 0) {
            strtab_intern(t, n->attrs[i].name, n->attrs[i].res_id);
        }
    }
    for (i = 0; i < n->n_children; i++) collect_resid_attr_names(n->children[i], t);
}

/* Pass 2: intern everything else (element names, unprefixed/unknown
 * attribute names, string attribute values). res_id is always 0 here --
 * only pass 1 entries participate in the resource map. */
static void collect_other_strings(axml_node *n, strtab *t) {
    size_t i;
    strtab_intern(t, n->name, 0);
    for (i = 0; i < n->n_attrs; i++) {
        strtab_intern(t, n->attrs[i].name, 0);
        if (n->attrs[i].type == AXML_ATTR_STRING) {
            strtab_intern(t, n->attrs[i].str_value, 0);
        }
    }
    for (i = 0; i < n->n_children; i++) collect_other_strings(n->children[i], t);
}

static void strtab_free(strtab *t) {
    size_t i;
    for (i = 0; i < t->n; i++) free(t->strs[i]);
    free(t->strs);
    free(t->res_ids);
}

/* UTF-16LE-encoded string pool, ASCII-only strings assumed (true for every
 * manifest string this backend generates), so 1 char == 1 UTF-16 code
 * unit and no surrogate handling is needed. */
static void build_string_pool_chunk(bytebuf *out, strtab *t) {
    bytebuf header, offsets, strdata;
    uint32_t stringsStart;
    size_t i;

    bb_init(&header); bb_init(&offsets); bb_init(&strdata);

    for (i = 0; i < t->n; i++) {
        const char *s = t->strs[i];
        size_t slen = strlen(s);
        size_t j;
        bb_u32(&offsets, (uint32_t)strdata.len);
        /* our strings are always well under 0x8000 code units */
        bb_u16(&strdata, (uint16_t)slen);
        for (j = 0; j < slen; j++) bb_u16(&strdata, (unsigned char)s[j]);
        bb_u16(&strdata, 0); /* null terminator */
    }
    bb_pad4(&strdata);

    stringsStart = (uint32_t)(28 + offsets.len); /* header(28) + offsets array, no style offsets */

    bb_u16(&header, RES_STRING_POOL_TYPE);
    bb_u16(&header, 28); /* headerSize */
    bb_u32(&header, (uint32_t)(28 + offsets.len + strdata.len)); /* size, patched below via recompute */
    bb_u32(&header, (uint32_t)t->n); /* stringCount */
    bb_u32(&header, 0);              /* styleCount */
    bb_u32(&header, 0);              /* flags: UTF-16, unsorted */
    bb_u32(&header, stringsStart);
    bb_u32(&header, 0);              /* stylesStart */

    bb_append(out, header.data, header.len);
    bb_append(out, offsets.data, offsets.len);
    bb_append(out, strdata.data, strdata.len);

    bb_free(&header); bb_free(&offsets); bb_free(&strdata);
}

static void build_resource_map_chunk(bytebuf *out, strtab *t, size_t map_count) {
    size_t i;
    bb_u16(out, RES_XML_RESOURCE_MAP_TYPE);
    bb_u16(out, 8); /* headerSize */
    bb_u32(out, (uint32_t)(8 + map_count * 4));
    for (i = 0; i < map_count; i++) bb_u32(out, t->res_ids[i]);
}

static uint32_t find_str(strtab *t, const char *s) {
    size_t i;
    if (!s) return NO_REF;
    for (i = 0; i < t->n; i++) if (strcmp(t->strs[i], s) == 0) return (uint32_t)i;
    return NO_REF;
}

static void emit_node_header(bytebuf *out, uint16_t type, uint32_t chunk_size) {
    bb_u16(out, type);
    bb_u16(out, 16); /* node headerSize */
    bb_u32(out, chunk_size);
    bb_u32(out, 1);       /* lineNumber */
    bb_u32(out, NO_REF);  /* comment */
}

static void emit_element(bytebuf *out, axml_node *n, strtab *t) {
    bytebuf attrs;
    size_t i;
    uint32_t chunk_size;

    bb_init(&attrs);
    for (i = 0; i < n->n_attrs; i++) {
        axml_attr *a = &n->attrs[i];
        bb_u32(&attrs, find_str(t, a->ns_uri));
        bb_u32(&attrs, find_str(t, a->name));
        if (a->type == AXML_ATTR_STRING) {
            uint32_t sidx = find_str(t, a->str_value);
            bb_u32(&attrs, sidx);           /* rawValue */
            bb_u16(&attrs, 8);              /* Res_value.size */
            { unsigned char z = 0; bb_append(&attrs, &z, 1); } /* res0 */
            { unsigned char dt = TYPE_STRING; bb_append(&attrs, &dt, 1); }
            bb_u32(&attrs, sidx);           /* typedValue.data = string index */
        } else {
            unsigned char dt;
            uint32_t data;
            bb_u32(&attrs, NO_REF);         /* rawValue: none */
            bb_u16(&attrs, 8);
            { unsigned char z = 0; bb_append(&attrs, &z, 1); }
            dt = (a->type == AXML_ATTR_INT_HEX) ? TYPE_INT_HEX :
                 (a->type == AXML_ATTR_INT_BOOLEAN) ? TYPE_INT_BOOLEAN : TYPE_INT_DEC;
            bb_append(&attrs, &dt, 1);
            data = (a->type == AXML_ATTR_INT_BOOLEAN) ? (a->int_value ? 0xFFFFFFFFu : 0u)
                                                       : (uint32_t)a->int_value;
            bb_u32(&attrs, data);
        }
    }

    chunk_size = (uint32_t)(16 /* node header */ + 20 /* attrExt */ + attrs.len);

    emit_node_header(out, RES_XML_START_ELEMENT_TYPE, chunk_size);
    bb_u32(out, find_str(t, n->ns_uri));
    bb_u32(out, find_str(t, n->name));
    bb_u16(out, 20); /* attributeStart */
    bb_u16(out, 20); /* attributeSize */
    bb_u16(out, (uint16_t)n->n_attrs);
    bb_u16(out, 0);  /* idIndex */
    bb_u16(out, 0);  /* classIndex */
    bb_u16(out, 0);  /* styleIndex */
    bb_append(out, attrs.data, attrs.len);
    bb_free(&attrs);

    for (i = 0; i < n->n_children; i++) emit_element(out, n->children[i], t);

    emit_node_header(out, RES_XML_END_ELEMENT_TYPE, 16 + 8);
    bb_u32(out, find_str(t, n->ns_uri));
    bb_u32(out, find_str(t, n->name));
}

int axml_serialize(axml_node *root, const char *ns_prefix, const char *ns_uri,
                    unsigned char **out_data, size_t *out_len) {
    strtab t;
    bytebuf pool_chunk, map_chunk, nodes, doc;
    size_t map_count;
    uint32_t prefix_idx, uri_idx;

    strtab_init(&t);
    collect_resid_attr_names(root, &t);
    map_count = t.n;

    strtab_intern(&t, ns_uri, 0);
    strtab_intern(&t, ns_prefix, 0);
    collect_other_strings(root, &t);

    bb_init(&pool_chunk);
    build_string_pool_chunk(&pool_chunk, &t);

    bb_init(&map_chunk);
    build_resource_map_chunk(&map_chunk, &t, map_count);

    prefix_idx = find_str(&t, ns_prefix);
    uri_idx = find_str(&t, ns_uri);

    bb_init(&nodes);
    emit_node_header(&nodes, RES_XML_START_NAMESPACE_TYPE, 16 + 8);
    bb_u32(&nodes, prefix_idx);
    bb_u32(&nodes, uri_idx);

    emit_element(&nodes, root, &t);

    emit_node_header(&nodes, RES_XML_END_NAMESPACE_TYPE, 16 + 8);
    bb_u32(&nodes, prefix_idx);
    bb_u32(&nodes, uri_idx);

    bb_init(&doc);
    {
        uint32_t total = (uint32_t)(8 + pool_chunk.len + map_chunk.len + nodes.len);
        bb_u16(&doc, RES_XML_TYPE);
        bb_u16(&doc, 8); /* headerSize */
        bb_u32(&doc, total);
    }
    bb_append(&doc, pool_chunk.data, pool_chunk.len);
    bb_append(&doc, map_chunk.data, map_chunk.len);
    bb_append(&doc, nodes.data, nodes.len);

    bb_free(&pool_chunk);
    bb_free(&map_chunk);
    bb_free(&nodes);
    strtab_free(&t);

    *out_data = doc.data;
    *out_len = doc.len;
    return 0;
}
