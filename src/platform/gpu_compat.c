// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef OPENNOW_GPU
#include <stddef.h>
// Mesa's C-defined context TLS has a constant zero initializer. Clang's
// C++ consumers also reference an optional initialization thunk.
void _ZTH23_mesa_glapi_tls_Context(void) {}
// Optional SDK diagnostics. The title records its own GPU/media counters.
int sceKernelDebugOutText(int channel,const char* text) { (void)channel;(void)text;return 0; }
#endif
