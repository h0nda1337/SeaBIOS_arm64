/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Minimal AArch64 handoff target; not an OS.  x0 receives the DTB. */
.section .text, "ax"
.global diagnostic_start
.type diagnostic_start, %function
diagnostic_start:
    ldr x1, =0x09000000
    adr x2, .Lmsg
.Lnext:
    ldrb w3, [x2], #1
    cbz w3, .Lwait
.Ltx:
    ldr w4, [x1, #0x18]
    tbnz w4, #5, .Ltx
    str w3, [x1]
    b .Lnext
.Lwait:
    wfe
    b .Lwait
.Lmsg:
    .asciz "\r\n[arm64] CHAINLOAD OK - diagnostic ARM64 payload running!\r\n"
