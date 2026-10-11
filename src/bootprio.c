// Shared boot-priority string matching extracted from src/boot.c.
// Copyright (C) 2008-2013 Kevin O'Connor and SeaBIOS contributors.
// LGPLv3: use with the original SeaBIOS sources and both architecture builds.
#include "bootprio.h"

// Preserve the original matching semantics; it is NOT a general fnmatch.
char *
bootprio_glob_prefix(const char *glob, const char *str)
{
    for (;;) {
        if (!*glob && (!*str || *str == '/'))
            return (char*)str;
        if (*glob == '*') {
            if (!*str || *str == '/' || *str == glob[1])
                glob++;
            else
                str++;
            continue;
        }
        if (*glob != *str)
            return 0;
        glob++;
        str++;
    }
}

int
bootprio_find_text(const char *glob, const char *data, unsigned length)
{
    // Firmware config is untrusted input. Limit file and line length; the
    // x86 build still uses its historical NUL-terminated bootorder behavior.
    if (!glob || !data || length > 4096u)
        return -1;
    // QEMU terminates the fw_cfg bootorder item with a NUL byte. It is a
    // file of explicit length, not a C string; strip only a final NUL.
    if (length && data[length-1] == 0)
        length--;
    unsigned start = 0;
    int rank = 1;
    while (start < length) {
        unsigned end = start;
        while (end < length && data[end] != '\n')
            end++;
        unsigned last = end;
        while (last > start && (data[last-1] == ' ' ||
                                data[last-1] == '\t' ||
                                data[last-1] == '\r'))
            last--;
        unsigned n = last - start;
        if (n < 256u) {
            char path[256];
            for (unsigned i = 0; i < n; ++i) {
                if (!data[start+i])
                    return -1;
                path[i] = data[start+i];
            }
            path[n] = 0;
            if (bootprio_glob_prefix(glob, path))
                return rank;
        }
        if (end == length)
            break;
        start = end + 1;
        rank++;
    }
    return -1;
}

/* Factored from the sorted BootList insertion in upstream src/boot.c.
 * Keep identical priorities stable; x86 drive type/controller sorting wins
 * on ties, as in SeaBIOS's long-standing IPL implementation. */
int
bootprio_precedes(const struct bootprio_key *a, const struct bootprio_key *b)
{
    if (a->priority != b->priority)
        return a->priority < b->priority;
    if (a->type != b->type)
        return a->type < b->type;
    if (a->type >= 1 && a->type <= 3) {
        if (a->drive_type != b->drive_type)
            return a->drive_type < b->drive_type;
        return a->controller_id < b->controller_id;
    }
    return 0;
}
