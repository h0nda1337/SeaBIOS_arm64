// Architecture-neutral POST stage order shared by x86 SeaBIOS and AArch64.
// Copyright (C) SeaBIOS contributors. Distributed under the GNU LGPLv3.
#ifndef __POSTFLOW_H
#define __POSTFLOW_H

/* Stages preserve upstream src/post.c maininit() ordering. An architecture
 * may omit phases that have no equivalent (e.g. x86 option ROMs on ARM64),
 * but must never silently pretend to have implemented a PC BIOS service. */
enum postflow_stage {
    POSTFLOW_INTERFACE = 0,
    POSTFLOW_PLATFORM,
    POSTFLOW_ASYNC_DEVICES,
    POSTFLOW_VIDEO,
    POSTFLOW_SYNC_DEVICES,
    POSTFLOW_OPTIONROMS,
    POSTFLOW_BOOTMENU,
    POSTFLOW_PREPARE,
    POSTFLOW_PROTECT,
    POSTFLOW_BOOT,
    POSTFLOW_STAGE_COUNT
};

/* Callback returns 0 for success; any nonzero value aborts the pipeline.
 * POSTFLOW_BOOT normally never returns after a successful handoff. */
struct postflow_ops {
    int (*step)(void *opaque, enum postflow_stage stage);
};

int postflow_run(const struct postflow_ops *ops, void *opaque,
                 enum postflow_stage *failed_stage);

#endif
