#ifndef __glu_h__
#define __glu_h__

/* GLU for Banana OS: the camera helpers (part of USE_GL = 1) */

#include "gl.h"

#ifdef __cplusplus
extern "C" {
#endif

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar);
void gluLookAt(GLdouble eyeX, GLdouble eyeY, GLdouble eyeZ, GLdouble centerX, GLdouble centerY, GLdouble centerZ,
               GLdouble upX, GLdouble upY, GLdouble upZ);
void gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top);
const GLubyte* gluErrorString(GLenum error);

#ifdef __cplusplus
}
#endif

#endif
