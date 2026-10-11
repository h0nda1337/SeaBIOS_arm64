/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Native AArch64 payload for legacy MBR/FAT32 chainloader smoke tests. */
.section .text, "ax"
.balign 4
.global legacy_stage_start
legacy_stage_start:
    ldr w7, [x0]
    movz w8, #0x0dd0
    movk w8, #0xedfe, lsl #16
    cmp w7, w8
    b.ne .Lfdtbad
    adr x5, .Ltail
    ldrb w6, [x5]
    cmp w6, #0x5c
    b.ne .Lfilebad
    adr x2, .Lsuccess
    b .Lprint
.Lfdtbad:
    adr x2, .Lfdtfail
    b .Lprint
.Lfilebad:
    adr x2, .Lfilefail
.Lprint:
    ldr x1, =0x09000000
.Lnext:
    ldrb w3, [x2], #1
    cbz w3, .Lhalt
.Lwaittx:
    ldr w4, [x1, #0x18]
    tbnz w4, #5, .Lwaittx
    str w3, [x1]
    b .Lnext
.Lhalt:
    wfe
    b .Lhalt
.Lsuccess:
    .asciz "\r\n[legacy] NATIVE AARCH64 MBR FAT32 STAGE2 OK (not Windows)\r\n"
.Lfdtfail:
    .asciz "\r\n[legacy] STAGE2 FAILED: BAD FDT\r\n"
.Lfilefail:
    .asciz "\r\n[legacy] STAGE2 FAILED: TRUNCATED FILE\r\n"
    .org 1023
.Ltail:
    .byte 0x5c
