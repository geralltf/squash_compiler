#ifndef GLFW3_H
#define GLFW3_H

#include "include/GL/gl.h"

/* ---- GLFW 3 minimal header for squash compiler ----
 * Callback types are declared as void* because squash does not parse
 * function-pointer typedef syntax.  On x86-64 Linux a function pointer
 * and a data pointer are both 8 bytes, so passing the address is safe. */

typedef struct GLFWwindow  GLFWwindow;
typedef struct GLFWmonitor GLFWmonitor;

/* ---- Init / terminate ---- */
int  glfwInit(void);
void glfwTerminate(void);

/* ---- Error ---- */
void *glfwSetErrorCallback(void *cb);

/* ---- Window hints ---- */
void glfwWindowHint(int hint, int value);

/* ---- Window creation / management ---- */
GLFWwindow *glfwCreateWindow(int w, int h, const char *title,
                              GLFWmonitor *mon, GLFWwindow *share);
void glfwDestroyWindow(GLFWwindow *win);
int  glfwWindowShouldClose(GLFWwindow *win);
void glfwSetWindowShouldClose(GLFWwindow *win, int value);
void glfwGetWindowSize(GLFWwindow *win, int *w, int *h);
void glfwGetFramebufferSize(GLFWwindow *win, int *w, int *h);
void glfwSetWindowTitle(GLFWwindow *win, const char *title);

/* ---- Context ---- */
void glfwMakeContextCurrent(GLFWwindow *win);
void glfwSwapBuffers(GLFWwindow *win);
void glfwSwapInterval(int interval);

/* ---- Events ---- */
void   glfwPollEvents(void);
void   glfwWaitEvents(void);
double glfwGetTime(void);

/* ---- Input ---- */
int glfwGetKey(GLFWwindow *win, int key);

/* ---- Callbacks (void* avoids function-pointer typedef syntax) ---- */
void *glfwSetKeyCallback(GLFWwindow *win, void *cb);
void *glfwSetFramebufferSizeCallback(GLFWwindow *win, void *cb);

/* ---- Window hints ---- */
#define GLFW_CONTEXT_VERSION_MAJOR  0x00022002
#define GLFW_CONTEXT_VERSION_MINOR  0x00022003
#define GLFW_OPENGL_PROFILE         0x00022008
#define GLFW_OPENGL_CORE_PROFILE    0x00032001
#define GLFW_OPENGL_COMPAT_PROFILE  0x00032002
#define GLFW_RESIZABLE              0x00020003
#define GLFW_VISIBLE                0x00020004
#define GLFW_DECORATED              0x00020005
#define GLFW_FLOATING               0x00020007
#define GLFW_DOUBLEBUFFER           0x00021010
#define GLFW_SAMPLES                0x0002100D

/* ---- Key codes (subset) ---- */
#define GLFW_KEY_UNKNOWN    -1
#define GLFW_KEY_SPACE      32
#define GLFW_KEY_ESCAPE    256
#define GLFW_KEY_ENTER     257
#define GLFW_KEY_TAB       258
#define GLFW_KEY_BACKSPACE 259
#define GLFW_KEY_RIGHT     262
#define GLFW_KEY_LEFT      263
#define GLFW_KEY_DOWN      264
#define GLFW_KEY_UP        265
#define GLFW_KEY_W         87
#define GLFW_KEY_A         65
#define GLFW_KEY_S         83
#define GLFW_KEY_D         68
#define GLFW_KEY_Q         81
#define GLFW_KEY_R         82
#define GLFW_KEY_F         70

/* ---- Key actions ---- */
#define GLFW_RELEASE  0
#define GLFW_PRESS    1
#define GLFW_REPEAT   2

/* ---- Mods ---- */
#define GLFW_MOD_SHIFT   0x0001
#define GLFW_MOD_CONTROL 0x0002
#define GLFW_MOD_ALT     0x0004

#endif
