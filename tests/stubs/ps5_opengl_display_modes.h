// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <EGL/egl.h>

#ifdef __cplusplus
extern "C" {
#endif
EGLBoolean eglSetDisplayModePS5(EGLDisplay display,EGLint width,EGLint height);
EGLBoolean eglSetDisplayRefreshPS5(EGLDisplay display,EGLint refresh_hz);
EGLBoolean eglGetDisplayModePS5(EGLDisplay display,EGLint* width,EGLint* height,EGLint* refresh_hz);
#ifdef __cplusplus
}
#endif
