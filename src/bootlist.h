// SeaBIOS boot-entry registration and ordering, shared across architectures.
// Copyright (C) 2008-2013 Kevin O'Connor and SeaBIOS contributors.
// This file may be distributed under the terms of the GNU LGPLv3 license.
#ifndef __BOOTLIST_H
#define __BOOTLIST_H

#include "list.h" // struct hlist_node, struct segoff_s

struct drive_s;

// Keep the original IPL record. The union retains native-width drive pointers
// on AArch64; data/vector are the original x86 option-ROM payloads.
struct bootentry_s {
    int type;
    union {
        u32 data;
        struct segoff_s vector;
        struct drive_s *drive;
    };
    int priority;
    const char *description;
    struct hlist_node node;
};

#define IPL_TYPE_FLOPPY      0x01
#define IPL_TYPE_HARDDISK    0x02
#define IPL_TYPE_CDROM       0x03
#define IPL_TYPE_CBFS        0x20
#define IPL_TYPE_BEV         0x80
#define IPL_TYPE_BCV         0x81
#define IPL_TYPE_HALT        0xf0
#define BOOT_PRIO_DEFAULT   9999

// Caller owns storage and sets data/vector/drive before registration. Entries
// must be unlinked; identical keys keep their original registration order.
void bootlist_add(struct hlist_head *list, struct bootentry_s *entry,
                  int type, int priority, const char *description);
void bootlist_move_first(struct hlist_head *list, struct bootentry_s *entry);

#endif // bootlist.h
