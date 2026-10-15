/* ---- sqw_ui_android.h: reusable Android UI component library surface ----
 *
 * Phase 7 of the approved UI-component-library plan: extraction of the
 * widget-interaction + real-soft-keyboard-typing logic proven working end-
 * to-end in android/examples/buttons_android.c (that file stays untouched
 * as the known-good regression reference -- it does NOT include this
 * header) into a genuinely reusable surface for OTHER apps, starting with
 * android/examples/forms_android.c.
 *
 * Deliberately header-with-static-functions, included directly into one
 * app's own translation unit (`#include "android/sqw_ui_android.h"`),
 * matching the exact convention this whole project already uses for
 * SQW/dom_walk.c, SQW/css.c, SQW/layout.c -- NOT a separately-compiled
 * library. Must be included AFTER "android/jni_min.h" (needs jobject/
 * jmethodID/jstring/jvalue and the sqw_jni_* wrapper functions) and after
 * SQW/layout.c (needs DomNode/LayoutList/SqwBoxKind/kind_for_tag/
 * dom_get_attr/css_apply_one/layout_hit_test).
 *
 * This header OWNS the shared document/viewport/touch/focus globals
 * (g_dom_root, g_layout, g_css_sheet, g_viewport_w, g_scroll_x/y,
 * g_touch_*, g_edit_text and friends, g_focused_box_index/g_has_focus,
 * g_form_nodes) -- every app using this header must populate
 * g_dom_root/g_layout/g_css_sheet/g_viewport_w itself (via its own
 * dom_parse()/css_apply()/layout_compute() call, exactly like
 * buttons_android.c's own build_page_dom_layout()) and must call
 * nativeTouchEvent/nativeEditTextReady/nativeTextChanged (all defined
 * here) as its real JNI-exported native methods -- see android_dex.c's
 * own onCreate() bytecode, which calls exactly these 3 by their real C
 * names via find_func_offset_a64().
 *
 * ---- Two real, confirmed ARM64-codegen caveats that shaped this header's
 * own design (see project_android_backend.md's "Full UI component library"
 * section for the full investigation) -- READ THESE before adding
 * anything that calls dom_walk():
 *   1. dom_walk() silently visits ZERO nodes when called through a
 *      separate wrapper function, even though the identical call inlined
 *      directly in the CALLER's own body works. This header therefore
 *      does NOT provide a "collect form nodes" wrapper function -- only
 *      the raw sqw_collect_form_nodes_visit() callback. Every app using
 *      this header MUST call `dom_walk(root, sqw_collect_form_nodes_visit,
 *      0);` directly, inline, in its OWN build_page_dom_layout()-equivalent
 *      function (see buttons_android.c's own build_page_dom_layout() for
 *      the exact call site shape to copy).
 *   2. dom_walk() ALSO silently visits zero nodes when called from deep
 *      inside a large, long-running function/loop (confirmed: a touch-
 *      handling for(;;) loop sitting below many large Vulkan-setup locals
 *      in the same function). Never call dom_walk() more than once, ever,
 *      at runtime -- the one inline call above (made early, in a small
 *      dedicated function) is the only one that should exist. */

/* ---- shared document/viewport state -- every app populates these via its
 * own dom_parse()/css_apply()/layout_compute() call. ---- */
static DomNode *g_dom_root = 0;
static LayoutList g_layout;
static CssStylesheet g_css_sheet;
static float g_viewport_w = 0, g_viewport_h = 0;
static float g_scroll_x = 0, g_scroll_y = 0;

/* ---- two-finger pinch-zoom/pan state. Composed with g_scroll_x/y as a
 * second, independent transform layer: finalScreenPixel = (contentPixel -
 * scroll) * zoomScale + zoomPan. g_zoom_scale is zero-initialized like
 * every other global here (never a non-zero static initializer) and
 * treated as "1.0, never pinched yet" via sqw_zoom_scale() below rather
 * than needing an explicit runtime init anywhere -- it only ever becomes
 * nonzero once a real pinch gesture has actually happened, and a real
 * pinch always clamps it into [SQW_ZOOM_MIN, SQW_ZOOM_MAX], so it never
 * needs resetting back to the sentinel. */
static float g_zoom_scale = 0;
static float g_zoom_pan_x = 0, g_zoom_pan_y = 0;
static int g_pinch_active = 0;
static float g_pinch_prev_dist = 0;
static float g_pinch_prev_mid_x = 0, g_pinch_prev_mid_y = 0;
#define SQW_ZOOM_MIN 0.5f
#define SQW_ZOOM_MAX 4.0f

static float sqw_zoom_scale(void) { return g_zoom_scale <= 0.0f ? 1.0f : g_zoom_scale; }

/* Self-contained Newton's-method sqrt -- deliberately NOT a call to the
 * real libm sqrtf, which project memory (SQW/sqo_host_syms.c) documents
 * as a real, confirmed-broken call shape on this toolchain in another
 * context (an indirect call combined with a float<->double ABI boundary
 * silently returns the input unchanged). This exact reimplementation is
 * already proven correct under both direct AND indirect calls there, so
 * it's reused verbatim here rather than risk the same failure mode again
 * on a code path (Android/bionic/ARM64) that has never called any libm
 * trig/sqrt function before. */
static float sqw_sqrtf(float x) {
    float guess; int i;
    if (x <= 0.0f) return 0.0f;
    guess = x;
    for (i = 0; i < 12; i++) guess = 0.5f * (guess + x / guess);
    return guess;
}

/* Inverse of the render-time content->screen transform -- every touch
 * hit-test must go through this (not raw "x + g_scroll_x") once zoom/pan
 * can be nonzero, or taps land on the wrong element exactly like the
 * vertical-surface-offset bug this project already found and fixed once
 * (see project_android_backend.md bug #24) -- same class of bug, new
 * transform layer. */
static void sqw_screen_to_content(float sx, float sy, float *outX, float *outY) {
    float zs = sqw_zoom_scale();
    *outX = (sx - g_zoom_pan_x) / zs + g_scroll_x;
    *outY = (sy - g_zoom_pan_y) / zs + g_scroll_y;
}

/* ---- touch handoff: nativeTouchEvent (Android main thread) -> render
 * thread. Simple polled-flag convention -- proven safe at this project's
 * scale (a human dragging a finger can't race a ~5ms poll loop in any way
 * that matters for a demo); single reader (the app's own render-thread
 * touch loop), single writer (nativeTouchEvent/nativeTextChanged, both
 * called on the Android UI thread, never concurrently with each other). */
static volatile float g_touch_x = 0, g_touch_y = 0;
static volatile int g_touch_action = -1;
static volatile int g_touch_pending = 0;

/* Raw touch coordinates delivered to nativeTouchEvent are in WINDOW/screen
 * space, but the SurfaceView that actually renders our content does not
 * start at screen-space (0,0) -- the Activity's window places it below its
 * title bar, confirmed on-device via pixel-precise screenshot analysis
 * (content measured at screen_y = content_y + ~340px while X has zero
 * offset). Corrected via a real View.getLocationOnScreen() JNI call against
 * the EditText (a sibling of the SurfaceView inside the same FrameLayout,
 * both positioned at the FrameLayout's own origin) -- subtracted from every
 * raw touch Y before it is used as a content coordinate anywhere.
 *
 * NOT queried in nativeEditTextReady: that runs during onCreate(), before
 * the view hierarchy's first layout pass, so getLocationOnScreen() would
 * see the EditText still sitting at its pre-layout (0,0) and bake in a
 * wrong, permanent zero offset (confirmed on-device: logged offset was
 * exactly 0,0 from that call site). Instead queried lazily, once, on the
 * first real touch event (nativeTouchEvent always runs well after the
 * first layout pass, since the user can't touch anything before the
 * window is actually shown) -- see sqw_ensure_surface_offset(). */
static int g_surface_offset_y = 0;
static int g_surface_offset_queried = 0;
static jmethodID g_m_edittext_getlocationonscreen = 0;

static void sqw_ensure_surface_offset(void *env) {
    if (g_surface_offset_queried) return;
    if (!g_edit_text || !g_m_edittext_getlocationonscreen) return;
    g_surface_offset_queried = 1;
    jintArray locArr = sqw_jni_NewIntArray(env, 2);
    jvalue locArgs[1];
    locArgs[0].l = locArr;
    sqw_jni_CallVoidMethodA(env, g_edit_text, g_m_edittext_getlocationonscreen, locArgs);
    jint locOut[2];
    sqw_jni_GetIntArrayRegion(env, locArr, 0, 2, locOut);
    g_surface_offset_y = (int)locOut[1];
    __android_log_print(4, "sqwui", "sqw_ensure_surface_offset: surface screen offset x=%d y=%d",
                         (int)locOut[0], g_surface_offset_y);
}

#define ACTION_DOWN 0
#define ACTION_UP 1
#define ACTION_MOVE 2
/* Not a real Android MotionEvent action -- reuses the exact same
 * g_touch_pending/g_touch_action single-slot queue nativeTouchEvent
 * already uses to wake the render thread's touch-handling loop, just with
 * a value that loop recognizes as "a DomNode::form_value changed
 * elsewhere (nativeTextChanged, real keystroke delivery), redraw, skip
 * all the hit-test/hover/drag logic that assumes real screen coordinates." */
#define ACTION_TEXT_CHANGED 3
/* Same synthetic-action convention as ACTION_TEXT_CHANGED above, this time
 * for "a pinch/two-finger-pan gesture changed g_zoom_scale/g_zoom_pan_x/y,
 * redraw" -- raised by nativeMultiTouchEvent below. */
#define ACTION_ZOOM_CHANGED 4

/* ---- real soft-keyboard text entry state: a real, persistent, off-
 * screen android.widget.EditText drives the REAL Android IME; the app's
 * own Vulkan glyph pipeline still renders all VISIBLE text. All zero-
 * initialized (never a non-zero initializer -- see g_focused_box_index's
 * own comment), with a separate "is anything focused" flag rather than a
 * "-1 means none" sentinel. ---- */
static jobject g_edit_text = 0;
static jmethodID g_m_edittext_settext = 0;
static jmethodID g_m_edittext_gettext = 0;
static jmethodID g_m_edittext_requestfocus = 0;
static jmethodID g_m_edittext_clearfocus = 0;
static jmethodID g_m_charsequence_tostring = 0;
static jobject g_imm = 0;
static jmethodID g_m_imm_showsoftinput = 0;
static jmethodID g_m_imm_hidesoftinputfromwindow = 0;
static jmethodID g_m_view_getwindowtoken = 0;
/* Which DomNode (by layout-box index into g_layout.boxes[], NOT a stored
 * DomNode* -- a real, confirmed squash/ARM64 bug: a global DomNode*
 * written from deep inside a large function's own loop reads back as a
 * wrong address on the next statement, see project_android_backend.md
 * bug #13) currently owns the keyboard. */
static int g_focused_box_index = 0;
static int g_has_focus = 0;

/* Tapping a text box / textarea requests real focus and raises the real
 * system keyboard. Runs directly inside nativeTouchEvent (NOT the render
 * thread's own touch-handling loop) specifically because `env` here is a
 * genuinely valid JNIEnv for nativeTouchEvent's OWN calling thread
 * (Android's real UI thread) -- JNIEnv values are thread-specific and the
 * render thread is a plain pthread with no JNIEnv of its own at all, so
 * any JNI call needed in response to a tap has to happen here,
 * synchronously, not be deferred through the usual g_touch_pending flag
 * handoff the Vulkan redraw path uses. */
static void sqw_dispatch_text_focus(void *env, float x, float y, int action) {
    if (action != ACTION_DOWN) return;
    if (!g_edit_text || g_layout.count == 0) return;
    float contentX, contentY;
    sqw_screen_to_content(x, y, &contentX, &contentY);
    int hit = layout_hit_test(&g_layout, contentX, contentY);
    if (hit < 0) return;
    LayoutBox *lb = &g_layout.boxes[hit];
    if (lb->kind != SQW_BOX_INPUT_TEXT && lb->kind != SQW_BOX_TEXTAREA) return;

    DomNode *node = lb->node;
    g_focused_box_index = hit;
    g_has_focus = 1;

    jstring currentVal = sqw_jni_NewStringUTF(env, node->form_value);
    jvalue setArgs[1];
    setArgs[0].l = currentVal;
    sqw_jni_CallVoidMethodA(env, g_edit_text, g_m_edittext_settext, setArgs);

    sqw_jni_CallBooleanMethodA(env, g_edit_text, g_m_edittext_requestfocus, 0);

    if (g_imm && g_m_imm_showsoftinput) {
        jvalue showArgs[2];
        showArgs[0].l = g_edit_text;
        showArgs[1].i = 0;
        sqw_jni_CallBooleanMethodA(env, g_imm, g_m_imm_showsoftinput, showArgs);
    }
    __android_log_print(4, "sqwui", "sqw_dispatch_text_focus: focused box=%d", hit);
}

/* x_bits/y_bits are the raw IEEE-754 bit patterns of the real touch
 * coordinates, NOT the coordinates themselves -- a real, confirmed
 * platform/runtime issue on real Android 17 hardware makes a plain
 * `float` argument to a squash-exported native method always arrive as
 * 0.0; the DEX shim (android/android_dex.c, see its own T_FLOAT_CLASS
 * comment) converts x/y to their raw bits via
 * java.lang.Float.floatToIntBits() before this call, sidestepping the
 * broken float-argument path entirely -- reinterpret them back into real
 * floats here via a pointer-cast reinterpretation (NOT a numeric
 * (float)cast, which would convert the integer VALUE instead of
 * reinterpreting its bits). */
void nativeTouchEvent(void *env, void *clazz, int x_bits, int y_bits, int action) {
    (void)clazz;
    float x = *(float *)&x_bits;
    float y = *(float *)&y_bits;
    /* Correct raw window-space Y into content-space Y once, here, before
     * ANY consumer (sqw_dispatch_text_focus below, and the render thread's
     * own touch-handling loop which reads g_touch_y) ever sees it -- see
     * g_surface_offset_y's own comment. X needs no correction (confirmed
     * zero X offset on-device). */
    sqw_ensure_surface_offset(env);
    y = y - (float)g_surface_offset_y;
    sqw_dispatch_text_focus(env, x, y, action);
    g_touch_x = x; g_touch_y = y; g_touch_action = action; g_touch_pending = 1;
}

/* Two-finger pinch-zoom/pan. Called from onTouchEvent ALONGSIDE (not
 * instead of) nativeTouchEvent above -- see android_dex.c's onTouchEvent
 * bytecode comment -- so single-finger tap/drag/text-focus behavior is
 * completely unaffected; this only ever acts when pointerCount>=2.
 *
 * `packed` is pointerCount<<4 | actionMasked (see that proto's own DEX
 * comment for why they're packed into one int rather than passed
 * separately -- format-35c invoke-static hard-caps at 5 register args).
 * x0/y0 are pointer 0's already-offset-corrected coordinates (same raw
 * bits nativeTouchEvent just computed, just reinterpreted again here --
 * cheap, avoids a 6th call parameter); x1/y1 are a SECOND pointer's
 * coordinates, always safe to read regardless of how many fingers are
 * actually down (see onTouchEvent's own Math.min(1,pointerCount-1)
 * comment) -- when there's only one finger, x1/y1 just redundantly equal
 * x0/y0, which this function ignores entirely via the pointerCount<2
 * early-return below. */
void nativeMultiTouchEvent(void *env, void *clazz, int packed, int x0_bits, int y0_bits, int x1_bits, int y1_bits) {
    (void)clazz;
    sqw_ensure_surface_offset(env);
    int pointerCount = packed >> 4;
    float x0 = *(float *)&x0_bits;
    float y0 = *(float *)&y0_bits;
    float x1 = *(float *)&x1_bits;
    float y1 = *(float *)&y1_bits;
    y0 = y0 - (float)g_surface_offset_y;
    y1 = y1 - (float)g_surface_offset_y;

    if (pointerCount < 2) {
        /* Second finger lifted (or never was down) -- end the gesture so
         * the NEXT two-finger touch starts a fresh anchor frame instead
         * of computing a huge, bogus delta against stale prev_dist/mid. */
        g_pinch_active = 0;
        return;
    }

    float dx = x1 - x0;
    float dy = y1 - y0;
    float dist = sqw_sqrtf(dx * dx + dy * dy);
    float midx = (x0 + x1) * 0.5f;
    float midy = (y0 + y1) * 0.5f;

    if (!g_pinch_active) {
        /* First frame of a new two-finger gesture: just record the
         * anchor, no delta to apply yet (matches the existing one-finger
         * drag's own g_drag_down_x/y-on-ACTION_DOWN convention). */
        g_pinch_active = 1;
        g_pinch_prev_dist = dist;
        g_pinch_prev_mid_x = midx;
        g_pinch_prev_mid_y = midy;
        return;
    }

    float scale = sqw_zoom_scale();
    if (g_pinch_prev_dist > 1.0f && dist > 1.0f) {
        float ratio = dist / g_pinch_prev_dist;
        float newScale = scale * ratio;
        if (newScale < SQW_ZOOM_MIN) newScale = SQW_ZOOM_MIN;
        if (newScale > SQW_ZOOM_MAX) newScale = SQW_ZOOM_MAX;
        /* Keep the content currently under the pinch midpoint fixed on
         * screen while the scale changes, exactly like a real multi-touch
         * zoom (pinch center stays put, everything else grows/shrinks
         * around it) -- see sqw_screen_to_content's own inverse-transform
         * comment for the forward transform this is solving for. */
        float appliedRatio = newScale / scale;
        g_zoom_pan_x = midx - (midx - g_zoom_pan_x) * appliedRatio;
        g_zoom_pan_y = midy - (midy - g_zoom_pan_y) * appliedRatio;
        g_zoom_scale = newScale;
    }
    /* Two-finger pan: the midpoint's own frame-to-frame screen movement,
     * layered on top of (independent of) the scale change above. */
    g_zoom_pan_x = g_zoom_pan_x + (midx - g_pinch_prev_mid_x);
    g_zoom_pan_y = g_zoom_pan_y + (midy - g_pinch_prev_mid_y);

    g_pinch_prev_dist = dist;
    g_pinch_prev_mid_x = midx;
    g_pinch_prev_mid_y = midy;

    g_touch_action = ACTION_ZOOM_CHANGED;
    g_touch_pending = 1;
}

/* The EditText is constructed once in onCreate() (see android_dex.c's
 * extended onCreate bytecode) and handed to native exactly once here. We
 * keep it alive past this call via NewGlobalRef (a local ref would be
 * freed once this native method returns) and resolve every jmethodID
 * we'll ever need against it up front -- GetMethodID/GetObjectClass are
 * real reflection-style lookups, not free, so doing this once here
 * instead of on every keystroke/tap is both correct (jmethodIDs are
 * stable for a class's lifetime) and the obviously right place for it. */
void nativeEditTextReady(void *env, void *clazz, void *editText) {
    (void)clazz;
    g_edit_text = sqw_jni_NewGlobalRef(env, (jobject)editText);
    jclass editTextClass = sqw_jni_GetObjectClass(env, g_edit_text);

    g_m_edittext_settext = sqw_jni_GetMethodID(env, editTextClass, "setText", "(Ljava/lang/CharSequence;)V");
    g_m_edittext_gettext = sqw_jni_GetMethodID(env, editTextClass, "getText", "()Ljava/lang/CharSequence;");
    g_m_edittext_requestfocus = sqw_jni_GetMethodID(env, editTextClass, "requestFocus", "()Z");
    g_m_edittext_clearfocus = sqw_jni_GetMethodID(env, editTextClass, "clearFocus", "()V");
    g_m_view_getwindowtoken = sqw_jni_GetMethodID(env, editTextClass, "getWindowToken", "()Landroid/os/IBinder;");

    /* toString() is declared on java.lang.Object, not EditText/TextView/
     * CharSequence -- resolved separately against that class, then called
     * later (via CallObjectMethodA) on whatever concrete object getText()
     * returns, exactly as real JNI code always does for an inherited
     * method. */
    jclass objectClass = sqw_jni_FindClass(env, "java/lang/Object");
    g_m_charsequence_tostring = sqw_jni_GetMethodID(env, objectClass, "toString", "()Ljava/lang/String;");

    /* InputMethodManager: obtained via the EditText's own getContext()
     * (the Activity it was constructed with), not a separate "this"
     * parameter -- nativeEditTextReady is a STATIC native method, so its
     * own "clazz" parameter is the SquashActivity jclass, not an
     * instance; a View's Context IS the real Activity instance that
     * created it, which is exactly what getSystemService() needs to be
     * called on. */
    jmethodID m_getcontext = sqw_jni_GetMethodID(env, editTextClass, "getContext", "()Landroid/content/Context;");
    jobject contextObj = sqw_jni_CallObjectMethodA(env, g_edit_text, m_getcontext, 0);
    jclass contextClass = sqw_jni_GetObjectClass(env, contextObj);
    jmethodID m_getsystemservice = sqw_jni_GetMethodID(env, contextClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring serviceName = sqw_jni_NewStringUTF(env, "input_method");
    jvalue svcArgs[1];
    svcArgs[0].l = serviceName;
    jobject imm = sqw_jni_CallObjectMethodA(env, contextObj, m_getsystemservice, svcArgs);
    g_imm = sqw_jni_NewGlobalRef(env, imm);
    jclass immClass = sqw_jni_GetObjectClass(env, g_imm);
    g_m_imm_showsoftinput = sqw_jni_GetMethodID(env, immClass, "showSoftInput", "(Landroid/view/View;I)Z");
    g_m_imm_hidesoftinputfromwindow = sqw_jni_GetMethodID(env, immClass, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z");

    /* Resolved here (jmethodIDs are stable for a class's lifetime, same as
     * every other method ID above) but deliberately NOT called yet -- see
     * sqw_ensure_surface_offset()'s own comment on why this has to wait
     * until the first real touch event. */
    g_m_edittext_getlocationonscreen = sqw_jni_GetMethodID(env, editTextClass, "getLocationOnScreen", "([I)V");

    __android_log_print(4, "sqwui", "nativeEditTextReady: edittext=%p imm=%p showSoftInput=%p hideSoftInput=%p",
                         (void *)g_edit_text, (void *)g_imm,
                         (void *)g_m_imm_showsoftinput, (void *)g_m_imm_hidesoftinputfromwindow);
}

/* Real keystroke delivery. Called by the hand-written
 * SquashTextWatcher.onTextChanged (see android_dex.c) on the Android UI
 * thread every time the real EditText's text changes -- exactly once per
 * edit, already converted to a real Java String on the Java side via
 * toString(). Copies it straight into the currently-focused DomNode's
 * form_value (bounded to that field's real 512-byte size) and wakes the
 * render thread via the existing touch-event queue with the synthetic
 * ACTION_TEXT_CHANGED action, so the new text actually appears through
 * the app's own glyph pipeline on the very next frame. */
void nativeTextChanged(void *env, void *clazz, void *text) {
    (void)clazz;
    if (!g_has_focus || g_focused_box_index < 0 || g_focused_box_index >= g_layout.count) return;
    DomNode *node = g_layout.boxes[g_focused_box_index].node;
    const char *cstr = sqw_jni_GetStringUTFChars(env, (jstring)text, 0);
    if (cstr) {
        strncpy(node->form_value, cstr, sizeof(node->form_value) - 1);
        node->form_value[sizeof(node->form_value) - 1] = 0;
        sqw_jni_ReleaseStringUTFChars(env, (jstring)text, cstr);
    }
    g_touch_action = ACTION_TEXT_CHANGED;
    g_touch_pending = 1;
}

/* ---- widget interaction: checkbox/radio toggle, label "for=" forwarding,
 * submit-field collection -- all pure C/DOM logic, no further JNI/DEX
 * surface beyond what's already above. Radio-group exclusivity and
 * submit-field collection are ported from SQW/sqw_main.c's own
 * radio_group_clear_visit()/form_collect_visit() (desktop walks only the
 * enclosing <form>; an app using this header that has no explicit <form>
 * wrapper walks the whole document instead -- same logic, wider root). ---- */

/* Flat list of every <input>/<textarea> in the document, built ONCE via a
 * dom_walk() call made directly inline inside the APP's own
 * build_page_dom_layout()-equivalent function (see this header's own top
 * comment, caveat #1) and reused by every interaction helper below
 * instead of calling dom_walk() again at runtime (caveat #2). */
#define SQW_MAX_FORM_NODES 32
static DomNode *g_form_nodes[SQW_MAX_FORM_NODES];
static int g_form_node_count = 0;
static void sqw_collect_form_nodes_visit(DomNode *node, int depth, void *ctx) {
    (void)depth; (void)ctx;
    if (g_form_node_count >= SQW_MAX_FORM_NODES) return;
    if (strcmp(node->tag, "input") == 0 || strcmp(node->tag, "textarea") == 0) {
        g_form_nodes[g_form_node_count++] = node;
    }
}

static DomNode *sqw_find_by_id(const char *id) {
    int i;
    if (!id) return 0;
    for (i = 0; i < g_form_node_count; i++) {
        const char *nid = dom_get_attr(g_form_nodes[i], "id");
        if (nid && strcmp(nid, id) == 0) return g_form_nodes[i];
    }
    return 0;
}

static void sqw_radio_group_clear(const char *name, DomNode *skip) {
    int i;
    for (i = 0; i < g_form_node_count; i++) {
        DomNode *node = g_form_nodes[i];
        if (node == skip) continue;
        const char *type = dom_get_attr(node, "type");
        if (!type || strcmp(type, "radio") != 0) continue;
        const char *nname = dom_get_attr(node, "name");
        if (nname && strcmp(nname, name) == 0) node->form_checked = 0;
    }
}

/* Returns 1 if `node` is a toggle-styled checkbox (class="toggle"), not a
 * plain checkbox -- SQW itself has no native <input type="toggle">, so a
 * toggle switch is just a checkbox with a different VISUAL (pill track +
 * sliding thumb instead of a bordered box + inset fill, left to the app's
 * own render pass to draw), sharing the exact same form_checked boolean
 * and SQW_BOX_INPUT_CHECK kind. */
static int sqw_node_is_toggle(DomNode *node) {
    const char *cls = dom_get_attr(node, "class");
    if (!cls) return 0;
    const char *p = cls;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        const char *tok = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        int tlen = (int)(p - tok);
        if (tlen == 6 && strncmp(tok, "toggle", 6) == 0) return 1;
    }
    return 0;
}

/* Click handling for a SQW_BOX_INPUT_CHECK box (checkbox, radio, or a
 * toggle -- all share this one box kind, see layout.c's own comment) --
 * ported 1:1 from SQW/sqw_main.c's own click handler: a radio sets itself
 * checked and clears every other same-"name" radio in the document; a
 * plain checkbox (or toggle) just flips its own form_checked. Re-resolves
 * CSS afterward in case a ":checked"-driven rule exists, matching the
 * same live re-style approach a real ":hover" handler already uses. */
static void sqw_toggle_check_or_radio(DomNode *node) {
    const char *type = dom_get_attr(node, "type");
    if (type && strcmp(type, "radio") == 0) {
        const char *name = dom_get_attr(node, "name");
        if (name) sqw_radio_group_clear(name, node);
        node->form_checked = 1;
    } else {
        node->form_checked = !node->form_checked;
    }
    css_apply_one(node, &g_css_sheet, g_viewport_w);
}

/* Submit: ported from SQW/sqw_main.c's own form_collect_visit(), but
 * local-only (no network) -- walks g_form_nodes[] collecting every named
 * input/textarea's current value, skipping unchecked checkboxes/radios
 * and the submit control itself. This is the shape other applications
 * using this as a component library would want: get the field values
 * back, decide what to do with them themselves -- no callback/URL
 * machinery imposed. */
#define SQW_MAX_FORM_FIELDS 32
typedef struct { const char *name; const char *value; } SqwFormField;
static int sqw_collect_form_fields(SqwFormField *out, int cap) {
    int count = 0, i;
    for (i = 0; i < g_form_node_count && count < cap; i++) {
        DomNode *node = g_form_nodes[i];
        const char *name = dom_get_attr(node, "name");
        if (!name) continue;
        const char *type = dom_get_attr(node, "type");
        if (type && (strcmp(type, "submit") == 0 || strcmp(type, "button") == 0 || strcmp(type, "reset") == 0)) continue;
        if (type && (strcmp(type, "checkbox") == 0 || strcmp(type, "radio") == 0) && !node->form_checked) continue;
        out[count].name = name;
        out[count].value = node->form_value;
        count++;
    }
    return count;
}

/* One tap-dispatch entry point for an app's own ACTION_UP handling: hit-
 * test result already in hand (`upHit`, from the app's own
 * layout_hit_test() call -- this header doesn't own hit-testing itself
 * since drag-vs-tap disambiguation is app-specific), this resolves label
 * "for=" forwarding, performs checkbox/radio toggling internally, and
 * tells the caller whether a redraw is needed and/or a submit control was
 * tapped (the app calls sqw_collect_form_fields() + does its own
 * logging/handling for that case -- kept separate since what an app DOES
 * with submitted fields is inherently app-specific). */
#define SQW_TAP_NONE 0
#define SQW_TAP_CHANGED 1
#define SQW_TAP_SUBMIT 2
/* kind_for_tag() alone can't distinguish a checkbox/radio <input> from a
 * plain text <input> -- it only ever sees the tag name "input" (real,
 * confirmed bug found via forms_android.c's label-forwarding: tapping a
 * checkbox's own, now-correctly-wide <label> re-derived its kind via
 * plain kind_for_tag(), which unconditionally returns SQW_BOX_INPUT_TEXT
 * for any "input" tag regardless of its real type= attribute -- see
 * kind_for_tag()'s own comment on why: the type-aware distinction only
 * exists in layout_compute()'s inline-child branch, never surfaced as a
 * reusable function before this. The checkbox's SQW_BOX_INPUT_CHECK
 * branch below was silently never reached, so tapping the label visibly
 * did nothing). This re-derives the real kind by also checking type=,
 * matching layout_compute()'s own logic, for any node reached via
 * re-targeting (label for=, button-caption text) rather than directly
 * from layout_hit_test() (which already got it right the first time). */
static SqwBoxKind sqw_kind_for_node(DomNode *node) {
    if (strcmp(node->tag, "input") == 0) {
        const char *type = dom_get_attr(node, "type");
        if (type && (strcmp(type, "checkbox") == 0 || strcmp(type, "radio") == 0)) return SQW_BOX_INPUT_CHECK;
        return SQW_BOX_INPUT_TEXT;
    }
    if (strcmp(node->tag, "textarea") == 0) return SQW_BOX_TEXTAREA;
    return kind_for_tag(node->tag);
}

static int sqw_ui_dispatch_tap(int upHit) {
    if (upHit < 0) return SQW_TAP_NONE;
    SqwBoxKind k = g_layout.boxes[upHit].kind;
    DomNode *tappedNode = g_layout.boxes[upHit].node;
    /* A tap landing on a button's own CAPTION rather than its background
     * hit-tests into that caption's own SQW_BOX_TEXT word-box, not the
     * button's box -- real for any button whose css_display ISN'T exactly
     * CSS_DISPLAY_INLINE (e.g. the common "display:inline-block" button
     * shape), since only a true atomic-inline box has no separate text
     * child at all (see layout.c's own is_atomic_inline_tag branch, and
     * node_direct_text()'s own comment on which button shape needs manual
     * caption drawing). Re-target to the owning element first, before the
     * label check below, so a tap anywhere on a button's own text still
     * counts as tapping the button. */
    if (k == SQW_BOX_TEXT && tappedNode->parent) {
        tappedNode = tappedNode->parent;
        k = sqw_kind_for_node(tappedNode);
    }
    if (k == SQW_BOX_SPAN && strcmp(tappedNode->tag, "label") == 0) {
        const char *forId = dom_get_attr(tappedNode, "for");
        DomNode *target = forId ? sqw_find_by_id(forId) : 0;
        if (target) { tappedNode = target; k = sqw_kind_for_node(target); }
    }
    if (k == SQW_BOX_INPUT_CHECK) {
        sqw_toggle_check_or_radio(tappedNode);
        return SQW_TAP_CHANGED;
    } else if (k == SQW_BOX_BUTTON) {
        const char *btype = dom_get_attr(tappedNode, "type");
        int isSubmit = (strcmp(tappedNode->tag, "button") == 0) && (!btype || strcmp(btype, "submit") == 0);
        if (isSubmit) return SQW_TAP_SUBMIT;
    }
    return SQW_TAP_NONE;
}

/* First direct text-node child's content -- used to draw a <button>'s own
 * label or a <label>'s own caption, neither of which get a separate
 * SQW_BOX_TEXT run of their own (both are "atomic inline" boxes sized via
 * direct_text_width() but never split into word-level sub-boxes, see
 * layout.c's own is_atomic_inline_tag branch) -- so a render pass reads
 * the text straight from the DOM, exactly the same way SQW's own desktop
 * renderer draws a <button>'s label from its child text / form_value. */
static const char *node_direct_text(DomNode *node, int *outLen) {
    int i;
    for (i = 0; i < node->child_count; i++) {
        DomNode *c = node->children[i];
        if (dom_is_text(c)) {
            *outLen = (int)strlen(c->text);
            return c->text;
        }
    }
    *outLen = 0;
    return "";
}
