#ifndef ANDROID_DEX_H
#define ANDROID_DEX_H
#include <stddef.h>

/* Builds a classes.dex containing exactly one class,
 * com.squash.runtime.SquashActivity (extends android.app.Activity,
 * implements android.view.SurfaceHolder$Callback). This is the fixed Java
 * bootstrap shim that replaces raw android.app.NativeActivity: it creates a
 * SurfaceView, registers itself as the SurfaceHolder callback, calls
 * System.loadLibrary(lib_name), and forwards surfaceCreated/surfaceChanged/
 * surfaceDestroyed to three native static methods
 * (nativeSurfaceCreated(Landroid/view/Surface;)V,
 *  nativeSurfaceChanged(II)V, nativeSurfaceDestroyed()V) that squash exports
 * from the app's own .so via standard JNI symbol names
 * (Java_com_squash_runtime_SquashActivity_nativeSurfaceCreated etc).
 *
 * `lib_name` is the bare .so name (as passed to System.loadLibrary), baked
 * into the dex's string pool at build time -- this is the only per-app
 * variable in an otherwise fixed class. Returns a malloc'd DEX file image
 * via *out_data (caller frees) and its length via *out_len. Returns 0 on
 * success. */
int android_dex_build_squash_activity_shim(const char *lib_name,
                                            unsigned char **out_data, size_t *out_len);

#endif
