#include "include/stdio.h"
#include "include/stdlib.h"
#include "include/GL/gl.h"
#include "include/GLFW/glfw3.h"

/* ---- globals ---- */
static int    g_width;   /* set in main — squash doesn't emit non-zero global initializers */
static int    g_height;
static double g_angle;

/* ---- callbacks ---- */
static void on_key(GLFWwindow *win, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(win, 1);
}

static void on_resize(GLFWwindow *win, int w, int h) {
    g_width  = w;
    g_height = h;
    glViewport(0, 0, w, h);
}

/* ---- draw one frame ---- */
static void draw_frame(void) {
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef((float)g_angle, 0.0f, 0.0f, 1.0f);

    /* RGB spinning triangle */
    glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.0f, 0.0f);  glVertex2f( 0.0f,  0.7f);
        glColor3f(0.0f, 1.0f, 0.0f);  glVertex2f(-0.6f, -0.4f);
        glColor3f(0.0f, 0.0f, 1.0f);  glVertex2f( 0.6f, -0.4f);
    glEnd();

    /* white square at centre */
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
        glVertex2f(-0.08f, -0.08f);
        glVertex2f( 0.08f, -0.08f);
        glVertex2f( 0.08f,  0.08f);
        glVertex2f(-0.08f,  0.08f);
    glEnd();
}

int main(void) {
    g_width  = 800;
    g_height = 600;
    g_angle  = 0.0;

    if (!glfwInit()) {
        printf("glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_DOUBLEBUFFER, 1);

    GLFWwindow *win = glfwCreateWindow(g_width, g_height,
                                       "squash OpenGL -- ESC to quit", 0, 0);
    if (!win) {
        printf("glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    glfwSetKeyCallback(win, on_key);
    glfwSetFramebufferSizeCallback(win, on_resize);

    glfwGetFramebufferSize(win, &g_width, &g_height);
    glViewport(0, 0, g_width, g_height);

    printf("Renderer : %s\n", (char *)glGetString(GL_RENDERER));
    printf("Version  : %s\n", (char *)glGetString(GL_VERSION));
    printf("Press ESC to quit.\n");
    fflush(0);

    double t0 = glfwGetTime();

    while (!glfwWindowShouldClose(win)) {
        double t1 = glfwGetTime();
        g_angle = g_angle + (t1 - t0) * 60.0;
        t0 = t1;

        draw_frame();
        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
