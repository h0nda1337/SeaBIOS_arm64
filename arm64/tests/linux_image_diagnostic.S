/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Synthetic standard arm64 Linux Image header to TEST raw kernel handoff.
 * This is NOT a Linux kernel, and must never be advertised as one. */
.section .text, "ax"
.balign 4
.global synthetic_image_start
synthetic_image_start:
    b synthetic_main
    .word 0
    .quad 0x80000 /* text_offset */
    .quad 4096    /* image_size: eight disk sectors */
    .quad 0       /* little-endian, any page size */
    .quad 0
    .quad 0
    .quad 0
    .word 0x644d5241  /* ARM\x64 */
    .word 0
synthetic_main:
    ldr x1, =0x09000000
    adr x2, .Lmsg
.Lloop:
    ldrb w3, [x2], #1
    cbz w3, .Lwait
.Ltx:
    ldr w4, [x1, #0x18]
    tbnz w4, #5, .Ltx
    str w3, [x1]
    b .Lloop
.Lwait:
    wfe
    b .Lwait
.Lmsg:
    .asciz "\r\n[linux] SYNTHETIC IMAGE HANDOFF OK (not Linux)\r\n"
