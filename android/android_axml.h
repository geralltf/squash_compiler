#ifndef ANDROID_AXML_H
#define ANDROID_AXML_H
#include <stdint.h>
#include <stddef.h>

/* Minimal builder + serializer for Android's binary XML format (AXML),
 * used for AndroidManifest.xml inside an APK. Format reference: AOSP
 * frameworks/base/libs/androidfw/include/androidfw/ResourceTypes.h
 * (ResChunk_header / ResStringPool_header / ResXMLTree_* structs). */

typedef enum {
    AXML_ATTR_STRING,     /* value is a string */
    AXML_ATTR_INT_DEC,    /* value is a plain decimal integer */
    AXML_ATTR_INT_HEX,    /* value is an integer, hex-typed */
    AXML_ATTR_INT_BOOLEAN /* value is boolean (0 or 1) */
} axml_attr_type;

typedef struct axml_node axml_node;

/* ns_uri may be NULL for an unprefixed element (e.g. <manifest>, <activity>). */
axml_node *axml_new_element(const char *ns_uri, const char *name);
void axml_free(axml_node *root);

void axml_add_child(axml_node *parent, axml_node *child);

/* ns_uri: NULL for an unprefixed attribute (e.g. manifest's "package").
 * res_id: the well-known android:* resource id for this attribute name
 * (e.g. 0x01010003 for "name"), or 0 if this attribute has no known
 * resource id (only meaningful/used when ns_uri is non-NULL). */
void axml_add_attr_string(axml_node *n, const char *ns_uri, const char *name,
                           uint32_t res_id, const char *value);
void axml_add_attr_int(axml_node *n, const char *ns_uri, const char *name,
                        uint32_t res_id, int32_t value, axml_attr_type type);

/* Serializes `root` as a full AndroidManifest.xml binary document, wrapped
 * in a single xmlns declaration (prefix -> uri) at the root level, matching
 * how AAPT emits manifests. Returns a malloc'd buffer via *out_data (caller
 * frees) and its length via *out_len. Returns 0 on success. */
int axml_serialize(axml_node *root, const char *ns_prefix, const char *ns_uri,
                    unsigned char **out_data, size_t *out_len);

#endif
