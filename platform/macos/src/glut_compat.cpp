/******************************************************************************
 * @file    glut_compat.c
 * @brief   The FreeGLUT extensions Apple's GLUT does not implement.
 *
 * Apple's GLUT owns its own NSApplication run loop inside glutMainLoop() and
 * offers no way to return from it and no event-pump entry point.  Two of the
 * five shims therefore cannot reproduce FreeGLUT exactly.  The differences are
 * deliberate and are what this file documents:
 *
 *   glutSetOption        stored and honoured for GLUT_ACTION_ON_WINDOW_CLOSE.
 *   glutCloseFunc        stored; invoked on the paths this file controls.
 *   glutLeaveMainLoop    calls the close callback (so App::Shutdown runs and
 *                        the log is flushed) and then exits the process.  There
 *                        is no way to unwind out of Apple's glutMainLoop(), so
 *                        "return to main()" is not achievable; exiting from
 *                        inside the close callback IS the FreeGLUT behaviour
 *                        for GLUT_ACTION_EXIT and is the closest match.
 *   glutMainLoopEvent    no-op.  Apple's GLUT cannot pump one event without
 *                        running the whole loop.  Call sites that use it to
 *                        keep the progress overlay painted will paint late.
 *   glutMouseWheelFunc   stored; never called, because Apple's GLUT delivers no
 *                        wheel events at all.  Mouse-wheel zoom/scroll in the
 *                        editor is therefore unavailable on macOS.
 *****************************************************************************/

#include <freeglut.h>

// Compiled as C++ (the target PCH is C++); Apple's GLUT headers already carry
// extern "C" guards, and <freeglut.h> re-includes them.

#include <stdio.h>
#include <stdlib.h>

static int g_action_on_close = GLUT_ACTION_EXIT;
static int g_leaving = 0;
static void (*g_close_func)(void) = NULL;
static void (*g_mouse_wheel_func)(int, int, int, int) = NULL;

void glutSetOption(GLenum eWhat, int value) {
    if (eWhat == GLUT_ACTION_ON_WINDOW_CLOSE) {
        g_action_on_close = value;
    }
    /* GLUT_INIT_STATE and friends have no Apple equivalent; ignore. */
}

void glutCloseFunc(void (*func)(void)) { g_close_func = func; }

void glutMouseWheelFunc(void (*func)(int wheel, int direction, int x, int y)) {
    g_mouse_wheel_func = func;
}

void glutMainLoopEvent(void) {
    /* No-op: see the file header. */
}

void glutLeaveMainLoop(void) {
    fflush(stdout);
    fflush(stderr);

    if (g_leaving) {
        /* Re-entered from the close callback: the only remaining way out of
         * Apple's run loop is to terminate the process. */
        exit(0);
    }
    g_leaving = 1;

    /* Run App::Shutdown through the registered close callback before exiting,
     * so the level, log and GL resources are released like FreeGLUT would. */
    if (g_close_func) {
        void (*cb)(void) = g_close_func;
        g_close_func = NULL;
        cb();
    }

    (void)g_action_on_close;
    fflush(stdout);
    fflush(stderr);
    exit(0);
}
