/******************************************************************************
 * @file    freeglut.h  (macOS shim)
 * @brief   Apple GLUT + the FreeGLUT extensions this code base actually uses.
 *
 * The editor is built against FreeGLUT on Windows.  macOS ships Apple's GLUT
 * framework, which implements the GLUT 3 API plus keyboard/special *up*
 * callbacks and GLUT_3_2_CORE_PROFILE, but not the FreeGLUT lifecycle and
 * wheel extensions:
 *
 *   glutSetOption / GLUT_ACTION_ON_WINDOW_CLOSE / ..._GLUTMAINLOOP_RETURNS
 *   glutLeaveMainLoop
 *   glutMainLoopEvent
 *   glutCloseFunc
 *   glutMouseWheelFunc
 *
 * Those five are provided by platform/macos/src/glut_compat.c.  Their
 * semantics on macOS differ from FreeGLUT where Apple's GLUT makes the
 * FreeGLUT behaviour impossible -- each difference is documented at its
 * definition, and in docs/parity/MACOS_NATIVE.md.
 *****************************************************************************/

#pragma once

#include <GLUT/glut.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FreeGLUT option identifiers (values match FreeGLUT's freeglut_std.h). */
#define GLUT_ACTION_ON_WINDOW_CLOSE 0x01F9
#define GLUT_ACTION_GLUTMAINLOOP_RETURNS 0x0001
#define GLUT_ACTION_CONTINUE_EXECUTION 0x0002
#define GLUT_ACTION_EXIT 0x0000
#define GLUT_INIT_STATE 0x007C

void glutSetOption(GLenum eWhat, int value);
void glutLeaveMainLoop(void);
void glutMainLoopEvent(void);
void glutCloseFunc(void (*func)(void));
void glutMouseWheelFunc(void (*func)(int wheel, int direction, int x, int y));

#ifdef __cplusplus
}
#endif
