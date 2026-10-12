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

    /* Install vectors at the current privilege level. */
    mrs     x0, CurrentEL
    cmp     x0, #8
    b.eq    .Lvector_el2
    cmp     x0, #4
    b.ne    .Lvector_done
    ldr     x0, =arm64_vectors_el1
    msr     vbar_el1, x0
    isb
    b       .Lvector_done
.Lvector_el2:
    ldr     x0, =arm64_vectors_el2
    msr     vbar_el2, x0
    isb
.Lvector_done:

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
    stp     xzr, xzr, [x1], #16
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
    stp     x0, x1, [sp, #-16]!
    bl      arm64_virtio_quiesce
    cbnz    x0, .Lhandoff_park
    ldp     x0, x1, [sp], #16
    mov     x4, x0
    mov     x0, x1
    mov     x1, xzr
    mov     x2, xzr
    mov     x3, xzr
    /* Ensure freshly loaded AArch64 instructions are visible at the PoU. */
    dsb     sy
    ic      iallu
    dsb     sy
    isb
    br      x4
.Lhandoff_park:
    wfe
    b       .Lhandoff_park
.size arm64_handoff, . - arm64_handoff

/* Clean a freshly loaded executable region to the Point of Coherency before
 * branch and instruction-cache invalidation.  This is essential on physical
 * ARM systems with data caches, even though QEMU's TCG is coherent.
 * x0=region base, x1=byte length. CTR_EL0.DminLine encodes a 4 << N
 * byte data-cache line. Caller validates address/length for overflow. */
.global arm64_cache_clean_range
.type arm64_cache_clean_range, %function
arm64_cache_clean_range:
    cbz     x1, .Lcache_done
    mrs     x2, ctr_el0
    ubfx    x2, x2, #16, #4
    mov     x3, #4
    lsl     x3, x3, x2
    sub     x4, x3, #1
    bic     x5, x0, x4
    adds    x6, x0, x1
    b.cs    .Lcache_done
.Lcache_loop:
    dc      cvac, x5
    add     x5, x5, x3
    cmp     x5, x6
    b.lo    .Lcache_loop
    dsb     sy
.Lcache_done:
    ret
.size arm64_cache_clean_range, . - arm64_cache_clean_range

/* 16 exception slots, each 128 bytes; vectors must be 2KiB aligned. */
.section .text.vectors, "ax"
.balign 2048
.global arm64_vectors_el1
arm64_vectors_el1:
.rept 16
    b arm64_vector_fault_el1
    .balign 128
.endr
.balign 2048
.global arm64_vectors_el2
arm64_vectors_el2:
.rept 16
    b arm64_vector_fault_el2
    .balign 128
.endr
.section .text, "ax"
arm64_vector_fault_el1:
    mov x0, #1
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    b arm64_exception_report
arm64_vector_fault_el2:
    mov x0, #2
    mrs x1, esr_el2
    mrs x2, elr_el2
    mrs x3, far_el2
    b arm64_exception_report
