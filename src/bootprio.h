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

/* Upstream src/boot.c bootentry_add() sort policy, shared with ARM64.
 * Device ordering applies only to floppy/HD/CD types (1..3).
 * Return 1 iff incoming should appear BEFORE existing (stable on ties).
 */
struct bootprio_key {
    int priority;
    int type;
    unsigned int drive_type;
    unsigned int controller_id;
};
int bootprio_precedes(const struct bootprio_key *incoming,
                      const struct bootprio_key *existing);

#endif
