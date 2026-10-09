/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/arm64.h"

/* Transitional logging shim.  The original SeaBIOS printf/output subsystem
 * needs architectural work; these arguments are deliberately not formatted. */
void arm64_debug_sink(int level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}

void arm64_warn_noalloc(void)
{
    uart_puts("[seabios] memory-map entry limit reached\n");
}
