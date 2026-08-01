#include "plic.h"

void plic_enable(int irq, int priority) {
    if (irq <= 0 || irq > 1023) return;

    /* Set source priority */
    PLIC_PRIORITY(irq) = (uint32_t)priority;

    /* Set enable bit for this source in hart 0 M-mode context */
    PLIC_ENABLE_REG(irq) |= (1u << (irq % 32));

    /* Accept all priorities (threshold = 0) */
    PLIC_THRESHOLD = 0;

    /* Enable M-mode external interrupts in mie */
    __asm__ volatile("csrs mie, %0" :: "r"(1 << 11) : "memory");
}
