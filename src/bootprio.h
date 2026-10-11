// Shared SeaBIOS bootorder matcher, used by x86 and AArch64.
// Copyright (C) SeaBIOS contributors; LGPLv3.
#ifndef __BOOTPRIO_SHARED_H
#define __BOOTPRIO_SHARED_H

// Original SeaBIOS glob matching contract: a terminal slash may match a
// prefix; '*' does not cross a '/' component boundary.
char *bootprio_glob_prefix(const char *glob, const char *str);

// Match a bounded fw_cfg bootorder file without assuming it is NUL terminated.
// The first line has priority 1 (including empty lines), as in src/boot.c.
// Returns -1 if there is no match or if the file is too large/malformed.
int bootprio_find_text(const char *glob, const char *data, unsigned length);

#endif
