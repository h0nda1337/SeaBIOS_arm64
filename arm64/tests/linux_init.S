/* A minimal no-libc Linux AArch64 init process, for testing *actual* kernel
 * handoff. It uses Linux syscalls, unlike our old bare-metal diagnostics. */
.text
.global _start
.type _start,%function
_start:
    mov x0, #1             // stdout (opened as /dev/console by the kernel)
    adr x1, message
    mov x2, #(message_end-message)
    mov x8, #64            // Linux AArch64 __NR_write
    svc #0
1:  wfe                   // Keep PID 1 alive until QEMU is stopped
    b 1b
message:
    .ascii "SEABIOS_ARM64_REAL_LINUX_USERSPACE_OK\n"
message_end:
