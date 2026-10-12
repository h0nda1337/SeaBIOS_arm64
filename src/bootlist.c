// BootList registration extracted from the original SeaBIOS src/boot.c.
// Copyright (C) 2008-2013 Kevin O'Connor and SeaBIOS contributors.
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "block.h" // struct drive_s
#include "bootlist.h" // struct bootentry_s
#include "bootprio.h" // bootprio_precedes

static struct bootprio_key
bootentry_key(const struct bootentry_s *entry)
{
    struct bootprio_key key = {
        .priority = entry->priority, .type = entry->type,
    };
    if (entry->type >= IPL_TYPE_FLOPPY && entry->type <= IPL_TYPE_CDROM
        && entry->drive) {
        key.drive_type = entry->drive->type;
        key.controller_id = entry->drive->cntl_id;
    }
    return key;
}

void
bootlist_add(struct hlist_head *list, struct bootentry_s *entry,
             int type, int priority, const char *description)
{
    entry->type = type;
    entry->priority = priority;
    entry->description = description ?: "?";

    // Original stable IPL sort: priority, IPL type, disk driver, controller.
    struct bootprio_key incoming = bootentry_key(entry);
    struct hlist_node **pprev;
    struct bootentry_s *pos;
    hlist_for_each_entry_pprev(pos, pprev, list, node) {
        struct bootprio_key existing = bootentry_key(pos);
        if (bootprio_precedes(&incoming, &existing))
            break;
    }
    hlist_add(&entry->node, pprev);
}

void
bootlist_move_first(struct hlist_head *list, struct bootentry_s *entry)
{
    // Preserve the original x86 boot-menu selection policy.
    hlist_del(&entry->node);
    entry->priority = 0;
    hlist_add_head(&entry->node, list);
}
