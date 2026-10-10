/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/arm64.h"

/* ARM generic system counter instead of the x86 PIT used by SeaBIOS.
 * Counter polling is the first step toward an architecture-neutral clock. */
static uint64_t TicksPerSecond;

void arm64_timer_init(void)
{
    uint64_t hz;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(hz));
    TicksPerSecond = hz;
    uart_puts("[timer] CNTFRQ_EL0: ");
    uart_dec((uint32_t)hz);
    uart_puts(" Hz\n");
}

uint64_t arm64_timer_now(void)
{
    uint64_t ticks;
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(ticks));
    return ticks;
}

/* Relative deadline; counter wrapping is resolved with modular arithmetic.
 * The caller must request a duration much shorter than 2^63 ticks. */
uint64_t arm64_timer_deadline_ms(uint32_t millis)
{
    uint64_t frequency = TicksPerSecond;
    if (!frequency || !millis || frequency > UINT64_MAX / millis)
        return 0;
    return arm64_timer_now() + (frequency * millis) / 1000u;
}

int arm64_timer_expired(uint64_t deadline)
{
    if (!deadline)
        return 0; /* bounded-spin fallback when no arch counter exists */
    return (int64_t)(arm64_timer_now() - deadline) >= 0;
}
