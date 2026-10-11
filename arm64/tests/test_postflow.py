#!/usr/bin/env python3
"""Exercise the architecture-neutral POST sequence with a native C harness.

Only ordering/error policy is tested here; device drivers need QEMU coverage.
"""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include "postflow.h"
#include <stdio.h>

static enum postflow_stage reached[POSTFLOW_STAGE_COUNT];
static unsigned calls;
static int failure;

static int step(void *context, enum postflow_stage stage) {
    if (context != &failure || stage >= POSTFLOW_STAGE_COUNT)
        return -99;
    reached[calls++] = stage;
    if ((int)stage == failure)
        return -17;
    return 0;
}

int main(void) {
    struct postflow_ops ops = { .step = step };
    enum postflow_stage stopped = POSTFLOW_STAGE_COUNT;
    if (postflow_run(0, 0, &stopped) != -1 ||
        stopped != POSTFLOW_STAGE_COUNT)
        return 1;
    failure = -1;
    calls = 0;
    if (postflow_run(&ops, &failure, &stopped) != 0 ||
        stopped != POSTFLOW_STAGE_COUNT || calls != POSTFLOW_STAGE_COUNT)
        return 2;
    for (int i = 0; i < POSTFLOW_STAGE_COUNT; i++)
        if ((int)reached[i] != i)
            return 3;
    for (int abort_stage = 0; abort_stage < POSTFLOW_STAGE_COUNT;
         abort_stage++) {
        failure = abort_stage;
        calls = 0;
        stopped = POSTFLOW_STAGE_COUNT;
        if (postflow_run(&ops, &failure, &stopped) != -17 ||
            (int)stopped != abort_stage || calls != (unsigned)abort_stage + 1)
            return 4;
    }
    puts("POSTFLOW: ordered stages, all abort points, invalid ops OK");
    return 0;
}
'''

class PostflowTests(unittest.TestCase):
    def test_x86_and_arm_shared_sequencer_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'harness.c'
            binary = Path(directory) / 'harness'
            source.write_text(HARNESS)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'src'), str(source),
                            str(ROOT / 'src/postflow.c'), '-o', str(binary)],
                           check=True)
            result = subprocess.run([str(binary)], check=True, text=True,
                                    capture_output=True)
            self.assertIn('ordered stages', result.stdout)

if __name__ == '__main__':
    unittest.main()
