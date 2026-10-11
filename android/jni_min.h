#ifndef SQUASH_JNI_MIN_H
#define SQUASH_JNI_MIN_H
#include <stdint.h>

/* Minimal, exactly-ordered subset of the real NDK `struct JNINativeInterface`
 * (ground truth: .../ndk/27.0.12077973/.../sysroot/usr/include/jni.h),
 * needed for this project's FIRST EVER native-calls-INTO-Java JNI usage
 * (everything built before this used only the other direction: Java calling
 * fixed, exported native symbols). Every call goes through this struct as a
 * function-pointer TABLE at a fixed byte offset per entry -- so every field
 * that comes before the one you actually want to call MUST be declared,
 * in the real SDK's exact order, even if this file never calls it, or every
 * later function pointer resolves to the wrong address and calls something
 * else entirely (silently, not a compile error).
 *
 * Deliberately generic `void *reservedN;` for every entry this file doesn't
 * itself call -- only the ones actually used below (FindClass, NewGlobalRef,
 * NewObjectA, GetObjectClass, GetMethodID, CallObjectMethodA,
 * CallBooleanMethodA, CallVoidMethodA, NewStringUTF, GetStringUTFChars,
 * ReleaseStringUTFChars) get their real, ABI-correct function pointer type;
 * every field's *position* still has to be right since it's a flat array of
 * same-sized (pointer) slots underneath, but only the CALLED fields need a
 * real signature. This also sidesteps a totally untested shape on this
 * backend: calling a C-variadic function ("...") through a function pointer
 * loaded from a struct field. The real JNINativeInterface has both a
 * variadic (`CallVoidMethod(...)`) and an array-based (`CallVoidMethodA`,
 * taking a `const jvalue*` built by hand) form of every Call*Method entry --
 * this file uses ONLY the "A" forms everywhere, on purpose, even though the
 * variadic ones sit at lower (earlier, already-needed-anyway) offsets in the
 * real table; they're declared here as plain `void *` padding and never
 * called. See project memory (project_android_backend.md) for this
 * project's own history of new ARM64 codegen bugs surfacing on previously
 * untested call *shapes* -- the "A" variants are exactly as real and valid a
 * part of the JNI spec as the variadic ones, just friendlier to this
 * backend's first attempt at calling through a C function-pointer table at
 * all. */

typedef uint8_t  jboolean;
typedef int8_t   jbyte;
typedef uint16_t jchar;
typedef int16_t  jshort;
typedef int32_t  jint;
typedef int64_t  jlong;
typedef float    jfloat;
typedef double   jdouble;
typedef jint     jsize;

/* Reference types: in real JNI C headers (not C++) every one of these is
 * just `void *` under the hood -- the "real" types (_jobject*, _jclass*,
 * ...) only exist as distinct pointer-to-incomplete-class types in the
 * C++ jni.h variant, which this project (plain C) never uses. */
typedef void *jobject;
typedef jobject jclass;
typedef jobject jstring;
typedef jobject jthrowable;

/* Opaque IDs -- real jni.h declares these as pointers to incomplete structs
 * (`struct _jmethodID; typedef struct _jmethodID *jmethodID;`) purely so the
 * compiler won't let you confuse a jmethodID with a jobject; squash doesn't
 * need the incomplete-struct forward declaration for that type-safety (this
 * file doesn't rely on the compiler catching that mistake), so these are
 * just opaque `void *` directly -- same real bit pattern either way. */
typedef void *jmethodID;
typedef void *jfieldID;

typedef union jvalue {
    jboolean z;
    jbyte    b;
    jchar    c;
    jshort   s;
    jint     i;
    jlong    j;
    jfloat   f;
    jdouble  d;
    jobject  l;
} jvalue;

typedef const struct JNINativeInterface *JNIEnv;

/* Field order below is the real NDK struct's order, verified field-by-field
 * against jni.h (171 entries total, indices 0-170; the last real field this
 * file declares, ReleaseStringUTFChars, is real index 170). Every function
 * pointer actually called by this project has its real signature; every
 * other slot is a same-size `void *reservedN;` placeholder preserving the
 * real struct's layout. Do NOT reorder, insert, or remove any field below
 * without re-deriving the correct ordering from a real ndk jni.h -- getting
 * even one slot wrong silently miscalls everything after it. */
struct JNINativeInterface {
    void *reserved0;
    void *reserved1;
    void *reserved2;
    void *reserved3;

    void *reserved4_GetVersion;

    void *reserved5_DefineClass;
    jclass (*FindClass)(JNIEnv *, const char *);

    void *reserved7_FromReflectedMethod;
    void *reserved8_FromReflectedField;
    void *reserved9_ToReflectedMethod;

    void *reserved10_GetSuperclass;
    void *reserved11_IsAssignableFrom;

    void *reserved12_ToReflectedField;

    void *reserved13_Throw;
    void *reserved14_ThrowNew;
    void *reserved15_ExceptionOccurred;
    void *reserved16_ExceptionDescribe;
    void *reserved17_ExceptionClear;
    void *reserved18_FatalError;

    void *reserved19_PushLocalFrame;
    void *reserved20_PopLocalFrame;

    jobject (*NewGlobalRef)(JNIEnv *, jobject);
    void *reserved22_DeleteGlobalRef;
    void *reserved23_DeleteLocalRef;
    void *reserved24_IsSameObject;

    void *reserved25_NewLocalRef;
    void *reserved26_EnsureLocalCapacity;

    void *reserved27_AllocObject;
    void *reserved28_NewObject;
    void *reserved29_NewObjectV;
    jobject (*NewObjectA)(JNIEnv *, jclass, jmethodID, const jvalue *);

    jclass (*GetObjectClass)(JNIEnv *, jobject);
    void *reserved32_IsInstanceOf;
    jmethodID (*GetMethodID)(JNIEnv *, jclass, const char *, const char *);

    void *reserved34_CallObjectMethod;
    void *reserved35_CallObjectMethodV;
    jobject (*CallObjectMethodA)(JNIEnv *, jobject, jmethodID, const jvalue *);
    void *reserved37_CallBooleanMethod;
    void *reserved38_CallBooleanMethodV;
    jboolean (*CallBooleanMethodA)(JNIEnv *, jobject, jmethodID, const jvalue *);
    void *reserved40_CallByteMethod;
    void *reserved41_CallByteMethodV;
    void *reserved42_CallByteMethodA;
    void *reserved43_CallCharMethod;
    void *reserved44_CallCharMethodV;
    void *reserved45_CallCharMethodA;
    void *reserved46_CallShortMethod;
    void *reserved47_CallShortMethodV;
    void *reserved48_CallShortMethodA;
    void *reserved49_CallIntMethod;
    void *reserved50_CallIntMethodV;
    void *reserved51_CallIntMethodA;
    void *reserved52_CallLongMethod;
    void *reserved53_CallLongMethodV;
    void *reserved54_CallLongMethodA;
    void *reserved55_CallFloatMethod;
    void *reserved56_CallFloatMethodV;
    void *reserved57_CallFloatMethodA;
    void *reserved58_CallDoubleMethod;
    void *reserved59_CallDoubleMethodV;
    void *reserved60_CallDoubleMethodA;
    void *reserved61_CallVoidMethod;
    void *reserved62_CallVoidMethodV;
    void (*CallVoidMethodA)(JNIEnv *, jobject, jmethodID, const jvalue *);

    void *reserved64_CallNonvirtualObjectMethod;
    void *reserved65_CallNonvirtualObjectMethodV;
    void *reserved66_CallNonvirtualObjectMethodA;
    void *reserved67_CallNonvirtualBooleanMethod;
    void *reserved68_CallNonvirtualBooleanMethodV;
    void *reserved69_CallNonvirtualBooleanMethodA;
    void *reserved70_CallNonvirtualByteMethod;
    void *reserved71_CallNonvirtualByteMethodV;
    void *reserved72_CallNonvirtualByteMethodA;
    void *reserved73_CallNonvirtualCharMethod;
    void *reserved74_CallNonvirtualCharMethodV;
    void *reserved75_CallNonvirtualCharMethodA;
    void *reserved76_CallNonvirtualShortMethod;
    void *reserved77_CallNonvirtualShortMethodV;
    void *reserved78_CallNonvirtualShortMethodA;
    void *reserved79_CallNonvirtualIntMethod;
    void *reserved80_CallNonvirtualIntMethodV;
    void *reserved81_CallNonvirtualIntMethodA;
    void *reserved82_CallNonvirtualLongMethod;
    void *reserved83_CallNonvirtualLongMethodV;
    void *reserved84_CallNonvirtualLongMethodA;
    void *reserved85_CallNonvirtualFloatMethod;
    void *reserved86_CallNonvirtualFloatMethodV;
    void *reserved87_CallNonvirtualFloatMethodA;
    void *reserved88_CallNonvirtualDoubleMethod;
    void *reserved89_CallNonvirtualDoubleMethodV;
    void *reserved90_CallNonvirtualDoubleMethodA;
    void *reserved91_CallNonvirtualVoidMethod;
    void *reserved92_CallNonvirtualVoidMethodV;
    void *reserved93_CallNonvirtualVoidMethodA;

    void *reserved94_GetFieldID;

    void *reserved95_GetObjectField;
    void *reserved96_GetBooleanField;
    void *reserved97_GetByteField;
    void *reserved98_GetCharField;
    void *reserved99_GetShortField;
    void *reserved100_GetIntField;
    void *reserved101_GetLongField;
    void *reserved102_GetFloatField;
    void *reserved103_GetDoubleField;

    void *reserved104_SetObjectField;
    void *reserved105_SetBooleanField;
    void *reserved106_SetByteField;
    void *reserved107_SetCharField;
    void *reserved108_SetShortField;
    void *reserved109_SetIntField;
    void *reserved110_SetLongField;
    void *reserved111_SetFloatField;
    void *reserved112_SetDoubleField;

    void *reserved113_GetStaticMethodID;

    void *reserved114_CallStaticObjectMethod;
    void *reserved115_CallStaticObjectMethodV;
    void *reserved116_CallStaticObjectMethodA;
    void *reserved117_CallStaticBooleanMethod;
    void *reserved118_CallStaticBooleanMethodV;
    void *reserved119_CallStaticBooleanMethodA;
    void *reserved120_CallStaticByteMethod;
    void *reserved121_CallStaticByteMethodV;
    void *reserved122_CallStaticByteMethodA;
    void *reserved123_CallStaticCharMethod;
    void *reserved124_CallStaticCharMethodV;
    void *reserved125_CallStaticCharMethodA;
    void *reserved126_CallStaticShortMethod;
    void *reserved127_CallStaticShortMethodV;
    void *reserved128_CallStaticShortMethodA;
    void *reserved129_CallStaticIntMethod;
    void *reserved130_CallStaticIntMethodV;
    void *reserved131_CallStaticIntMethodA;
    void *reserved132_CallStaticLongMethod;
    void *reserved133_CallStaticLongMethodV;
    void *reserved134_CallStaticLongMethodA;
    void *reserved135_CallStaticFloatMethod;
    void *reserved136_CallStaticFloatMethodV;
    void *reserved137_CallStaticFloatMethodA;
    void *reserved138_CallStaticDoubleMethod;
    void *reserved139_CallStaticDoubleMethodV;
    void *reserved140_CallStaticDoubleMethodA;
    void *reserved141_CallStaticVoidMethod;
    void *reserved142_CallStaticVoidMethodV;
    void *reserved143_CallStaticVoidMethodA;

    void *reserved144_GetStaticFieldID;

    void *reserved145_GetStaticObjectField;
    void *reserved146_GetStaticBooleanField;
    void *reserved147_GetStaticByteField;
    void *reserved148_GetStaticCharField;
    void *reserved149_GetStaticShortField;
    void *reserved150_GetStaticIntField;
    void *reserved151_GetStaticLongField;
    void *reserved152_GetStaticFloatField;
    void *reserved153_GetStaticDoubleField;

    void *reserved154_SetStaticObjectField;
    void *reserved155_SetStaticBooleanField;
    void *reserved156_SetStaticByteField;
    void *reserved157_SetStaticCharField;
    void *reserved158_SetStaticShortField;
    void *reserved159_SetStaticIntField;
    void *reserved160_SetStaticLongField;
    void *reserved161_SetStaticFloatField;
    void *reserved162_SetStaticDoubleField;

    void *reserved163_NewString;
    void *reserved164_GetStringLength;
    void *reserved165_GetStringChars;
    void *reserved166_ReleaseStringChars;
    jstring (*NewStringUTF)(JNIEnv *, const char *);
    void *reserved168_GetStringUTFLength;
    const char *(*GetStringUTFChars)(JNIEnv *, jstring, jboolean *);
    void (*ReleaseStringUTFChars)(JNIEnv *, jstring, const char *);
};

/* `env` as received by every native method is really a `JNIEnv *` --
 * i.e. `const struct JNINativeInterface **`, a pointer to the vtable
 * pointer, NOT the vtable pointer itself (this codebase has always treated
 * it as an opaque `void *env` placeholder before now, since nothing called
 * back into Java). Real JNI C usage is `(*env)->FindClass(env, ...)`: ONE
 * dereference of `env` gets the vtable pointer to read the function
 * pointer off of, but the ORIGINAL `env` (not the dereferenced vtable
 * pointer) is what gets passed as each JNI function's own first argument
 * (every function pointer in the struct below is itself declared to take
 * `JNIEnv *`, matching real jni.h). Getting this one level of indirection
 * wrong reads a garbage/zero function pointer and jumps to address 0 on
 * the very first call -- confirmed by direct reproduction on-device
 * (tombstone: "pc 0000000000000000", an earlier, wrong version of these
 * wrappers treated `env` as already being the single-pointer vtable
 * value). Each wrapper below takes the raw `void *env` native methods
 * already receive, re-establishes its real `JNIEnv *` type, dereferences
 * it exactly once for the vtable, and passes the ORIGINAL (re-typed, not
 * dereferenced) pointer as the first call argument. */
static jclass sqw_jni_FindClass(void *env, const char *name) {
    JNIEnv *penv = (JNIEnv *)env;
    JNIEnv vtable = *penv;
    return vtable->FindClass(penv, name);
}
static jobject sqw_jni_NewGlobalRef(void *env, jobject obj) {
    JNIEnv *penv = (JNIEnv *)env;
    JNIEnv vtable = *penv;
    return vtable->NewGlobalRef(penv, obj);
}
static jclass sqw_jni_GetObjectClass(void *env, jobject obj) {
    JNIEnv *penv = (JNIEnv *)env;
    JNIEnv vtable = *penv;
    return vtable->GetObjectClass(penv, obj);
}
static jmethodID sqw_jni_GetMethodID(void *env, jclass clazz, const char *name, const char *sig) {
    JNIEnv *penv = (JNIEnv *)env;
    JNIEnv vtable = *penv;
    return vtable->GetMethodID(penv, clazz, name, sig);
}
static jobject sqw_jni_NewObjectA(void *env, jclass clazz, jmethodID m, const jvalue *args) {
    JNIEnv *penv = (JNIEnv *)env;
    JNIEnv vtable = *penv;
    return vtable->NewObjectA(penv, clazz, m, args);
}
static jobject sqw_jni_CallObjectMethodA(void *env, jobject obj, jmethodID m, const jvalue *args) {
    JNIEnv *penv = (JNIEnv *)env;
    JNIEnv vtable = *penv;
    return vtable->CallObjectMethodA(penv, obj, m, args);
}
static jboolean sqw_jni_CallBooleanMethodA(void *env, jobject obj, jmethodID m, const jvalue *args) {
    JNIEnv *penv = (JNIEnv *)env;
    JNIEnv vtable = *penv;
    return vtable->CallBooleanMethodA(penv, obj, m, args);
}
static void sqw_jni_CallVoidMethodA(void *env, jobject obj, jmethodID m, const jvalue *args) {
    JNIEnv *penv = (JNIEnv *)env;
    JNIEnv vtable = *penv;
    vtable->CallVoidMethodA(penv, obj, m, args);
}
static jstring sqw_jni_NewStringUTF(void *env, const char *s) {
    JNIEnv *penv = (JNIEnv *)env;
    JNIEnv vtable = *penv;
    return vtable->NewStringUTF(penv, s);
}
static const char *sqw_jni_GetStringUTFChars(void *env, jstring s, jboolean *isCopy) {
    JNIEnv *penv = (JNIEnv *)env;
    JNIEnv vtable = *penv;
    return vtable->GetStringUTFChars(penv, s, isCopy);
}
static void sqw_jni_ReleaseStringUTFChars(void *env, jstring s, const char *utf) {
    JNIEnv *penv = (JNIEnv *)env;
    JNIEnv vtable = *penv;
    vtable->ReleaseStringUTFChars(penv, s, utf);
}

#endif /* SQUASH_JNI_MIN_H */
