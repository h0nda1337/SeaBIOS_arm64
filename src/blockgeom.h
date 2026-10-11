// SeaBIOS hard-disk geometry translation shared between x86 and AArch64.
// Extracted from src/block.c::setup_translation, LGPLv3.
#ifndef __BLOCKGEOM_H
#define __BLOCKGEOM_H
#include "block.h"

void blockgeom_translate(struct drive_s *drive, u8 translation);
const char *blockgeom_translation_name(u8 translation);

#endif
