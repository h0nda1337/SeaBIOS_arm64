/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* QEMU virt -bios / flash0 starts AArch64 firmware here. */
.section .text.boot, "ax"
.balign 4
.global _start
.type _start, %function
_start:
    /* Only the primary processor is allowed to use the shared stack. */
    mrs     x1, mpidr_el1
    mov     x2, #0xffff
    and     x1, x1, x2
    cbnz    x1, .Lpark

    msr     daifset, #0xf
    ldr     x0, =__stack_top
    mov     sp, x0

    /* Writable globals live in RAM; the executable image stays in flash. */
    ldr     x0, =__data_load
    ldr     x1, =__data_begin
    ldr     x2, =__data_end
.Lcopy:
    cmp     x1, x2
    b.hs    .Lclear_bss
    ldrb    w3, [x0], #1
    strb    w3, [x1], #1
    b       .Lcopy
.Lclear_bss:
    ldr     x1, =__bss_begin
    ldr     x2, =__bss_end
.Lzero:
    cmp     x1, x2
    b.hs    .Lrun
    strb    wzr, [x1], #1
    b       .Lzero
.Lrun:
    /* QEMU bare-metal boot: DTB is at the beginning of RAM. */
    ldr     x0, =0x40000000
    bl      boot_main
.Lpark:
    wfe
    b       .Lpark
.size _start, . - _start

.section .text, "ax"
.global arm64_handoff
.type arm64_handoff, %function
arm64_handoff:
    /* x0=entry, x1=DTB. No UEFI/SMCCC protocol is implied by this jump. */
    mov     x4, x0
    mov     x0, x1
    mov     x1, xzr
    mov     x2, xzr
    mov     x3, xzr
    dsb     sy
    isb
    br      x4
.size arm64_handoff, . - arm64_handoff
