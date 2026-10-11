/* Shared SeaBIOS block operation rules (LGPLv3).
 * Extracted from src/block.c so AArch64 can use the original disk_op ABI. */
#ifndef __BLOCKPOLICY_H
#define __BLOCKPOLICY_H
#include "block.h"

/* Return a BIOS disk status; on boundary errors, zero the block count. */
int blockop_prepare(struct disk_op_s *op, u16 block_size);
/* SeaBIOS convention: a backend failing without reporting a partial read
 * must leave count at zero. Return the backend's original status. */
int blockop_finish(struct disk_op_s *op, u16 original_count, int status);
#endif
