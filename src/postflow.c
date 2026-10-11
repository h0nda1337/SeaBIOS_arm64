// Shared SeaBIOS POST sequencing, factored from the original src/post.c.
// Copyright (C) SeaBIOS contributors. Distributed under the GNU LGPLv3.
#include "postflow.h"

int
postflow_run(const struct postflow_ops *ops, void *opaque,
             enum postflow_stage *failed_stage)
{
    if (failed_stage)
        *failed_stage = POSTFLOW_STAGE_COUNT;
    if (!ops || !ops->step)
        return -1;
    for (enum postflow_stage stage = POSTFLOW_INTERFACE;
         stage < POSTFLOW_STAGE_COUNT; stage++) {
        int status = ops->step(opaque, stage);
        if (status) {
            if (failed_stage)
                *failed_stage = stage;
            return status;
        }
    }
    return 0;
}
