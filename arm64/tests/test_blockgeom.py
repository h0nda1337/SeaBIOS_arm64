#!/usr/bin/env python3
"""Exercise the *same* extracted SeaBIOS geometry implementation as x86/AArch64."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TEST = r'''
#include "blockgeom.h"
#include <stdio.h>

static int cases, failures;
#define EXPECT(expr) do { cases++; if (!(expr)) { \
    fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #expr); failures++; \
} } while (0)

int main(void)
{
    struct drive_s d = {0};
    d.sectors = 0;
    blockgeom_translate(&d, TRANSLATION_LBA);
    EXPECT(d.lchs.head == 16 && d.lchs.cylinder == 0 && d.lchs.sector == 63);
    EXPECT(d.translation == TRANSLATION_LBA);
    d.sectors = 63ULL * 16 * 1024;
    blockgeom_translate(&d, TRANSLATION_LBA);
    EXPECT(d.lchs.cylinder == 1024 && d.lchs.head == 16);
    d.sectors = 63ULL * 255 * 1024 + 1;
    blockgeom_translate(&d, TRANSLATION_LBA);
    EXPECT(d.lchs.cylinder == 1024 && d.lchs.head == 255);
    d.sectors = ~0ULL;
    blockgeom_translate(&d, TRANSLATION_LBA);
    EXPECT(d.lchs.cylinder == 1024 && d.lchs.head == 255);
    d.pchs.cylinder = 2048; d.pchs.head = 16; d.pchs.sector = 63;
    blockgeom_translate(&d, TRANSLATION_LARGE);
    EXPECT(d.lchs.cylinder == 1024 && d.lchs.head == 32);
    d.pchs.cylinder = 61440; d.pchs.head = 16; d.pchs.sector = 63;
    blockgeom_translate(&d, TRANSLATION_RECHS);
    EXPECT(d.lchs.cylinder <= 1024 && d.lchs.head >= 15);
    d.pchs.cylinder = 19; d.pchs.head = 12; d.pchs.sector = 17;
    blockgeom_translate(&d, TRANSLATION_NONE);
    EXPECT(d.lchs.cylinder == 19 && d.lchs.head == 12 && d.lchs.sector == 17);
    d.lchs.cylinder = 700; d.lchs.head = 17; d.lchs.sector = 42;
    blockgeom_translate(&d, TRANSLATION_HOST);
    EXPECT(d.lchs.cylinder == 700 && d.lchs.head == 17 && d.lchs.sector == 42);
    EXPECT(blockgeom_translation_name(TRANSLATION_LBA)[0] == 'l');
    EXPECT(blockgeom_translation_name(TRANSLATION_HOST)[0] == 'h');
    EXPECT(blockgeom_translation_name(TRANSLATION_RECHS)[0] == 'r');
    EXPECT(blockgeom_translation_name(TRANSLATION_NONE)[0] == 'n');
    if (failures) return 1;
    printf("PASS: %d shared CHS geometry assertions\n", cases);
    return 0;
}
'''

def main():
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        (t/'test.c').write_text(TEST)
        subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror',
                        '-DCONFIG_AARCH64=1','-I'+str(ROOT/'src'),
                        str(t/'test.c'),str(ROOT/'src/blockgeom.c'),'-o',str(t/'test')],check=True)
        subprocess.run([str(t/'test')],check=True)

if __name__ == '__main__': main()
