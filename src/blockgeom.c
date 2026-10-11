// SeaBIOS disk geometry translation, factored from src/block.c.
// Copyright (C) SeaBIOS contributors; GNU LGPLv3.
// Both original x86 and the AArch64 port execute this identical algorithm.
#include "blockgeom.h"

const char *
blockgeom_translation_name(u8 translation)
{
    switch (translation) {
    case TRANSLATION_LBA: return "lba";
    case TRANSLATION_RECHS: return "r-echs";
    case TRANSLATION_LARGE: return "large";
    case TRANSLATION_HOST: return "host-supplied";
    default: return "none";
    }
}

void
blockgeom_translate(struct drive_s *drive, u8 translation)
{
    drive->translation = translation;
    u16 heads = drive->pchs.head;
    u16 cylinders = drive->pchs.cylinder;
    u16 spt = drive->pchs.sector;
    u64 sectors = drive->sectors;

    switch (translation) {
    default:
    case TRANSLATION_NONE:
        break;
    case TRANSLATION_LBA:
        spt = 63;
        if (sectors > 63*255*1024) {
            heads = 255;
            cylinders = 1024;
            break;
        }
        u32 sect = (u32)sectors / 63;
        heads = sect / 1024;
        if (heads>128)
            heads = 255;
        else if (heads>64)
            heads = 128;
        else if (heads>32)
            heads = 64;
        else if (heads>16)
            heads = 32;
        else
            heads = 16;
        cylinders = sect / heads;
        break;
    case TRANSLATION_RECHS:
        if (heads==16) {
            if (cylinders>61439)
                cylinders=61439;
            heads=15;
            cylinders = (u16)((u32)(cylinders)*16/15);
        }
        // Fall through to large geometry translation.
    case TRANSLATION_LARGE:
        while (cylinders > 1024) {
            cylinders >>= 1;
            heads <<= 1;
            if (heads > 127)
                break;
        }
        break;
    case TRANSLATION_HOST:
        cylinders = drive->lchs.cylinder;
        heads = drive->lchs.head;
        spt = drive->lchs.sector;
        break;
    }
    if (cylinders > 1024)
        cylinders = 1024;
    drive->lchs.head = heads;
    drive->lchs.cylinder = cylinders;
    drive->lchs.sector = spt;
}
