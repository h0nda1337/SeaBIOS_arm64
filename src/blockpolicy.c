// Architecture-independent SeaBIOS block operation policy, LGPLv3.
// These two rules are extracted from process_op() in src/block.c.
#include "blockpolicy.h"
#include "std/disk.h"

int
blockop_prepare(struct disk_op_s *op, u16 block_size)
{
    if ((u32)op->count * block_size > 64u*1024u) {
        op->count = 0;
        return DISK_RET_EBOUNDARY;
    }
    return DISK_RET_SUCCESS;
}

int
blockop_finish(struct disk_op_s *op, u16 original_count, int status)
{
    if (status && op->count == original_count)
        op->count = 0;
    return status;
}

/* Original SeaBIOS fallback command semantics, moved from src/block.c. */
int
default_process_op(struct disk_op_s *op)
{
    switch (op->command) {
    case CMD_FORMAT:
    case CMD_RESET:
    case CMD_ISREADY:
    case CMD_VERIFY:
    case CMD_SEEK:
        /* No-op success only when the underlying device lacks a handler. */
        return DISK_RET_SUCCESS;
    default:
        return DISK_RET_EPARAM;
    }
}
