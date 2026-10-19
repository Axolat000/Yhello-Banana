/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * libGL.so.1 for Banana OS, for LWJGL: GL.create() on Linux loads libGL.so.1
 * and takes the OpenGL functions from its glXGetProcAddress. Here they are
 * those of the current bgl3 context (Mesa), which libglfw.so (DT_NEEDED: the
 * SDK's dlopen gives back the libglfw.so LWJGL has already loaded) owns.
 */
#define API __attribute__((visibility("default")))

void* glfwBananaGetProcAddress(const char* name);

API void* glXGetProcAddress(const unsigned char* name) { return glfwBananaGetProcAddress((const char*)name); }
API void* glXGetProcAddressARB(const unsigned char* name) { return glfwBananaGetProcAddress((const char*)name); }
