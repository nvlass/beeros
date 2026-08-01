/* plic.h — RISC-V Platform-Level Interrupt Controller for QEMU virt
 *
 * PLIC base: 0x0C000000 (fixed on QEMU virt machine).
 * Hart 0, M-mode context = context 0.
 *
 * Interrupt flow:
 *   plic_enable(irq, priority)  — configure source, unmask for hart 0 M-mode
 *   <device raises IRQ>
 *   trap handler fires → plic_claim() → set pending bit → plic_complete(irq)
 *   beer task polls irq_pending → handler runs in task context
 */

#ifndef PLIC_H
#define PLIC_H

#include <stdint.h>

#define PLIC_BASE       0x0C000000UL

/* Per-source priority (1–7; 0 = disabled) */
#define PLIC_PRIORITY(n)  (*(volatile uint32_t*)(PLIC_BASE + (n) * 4))

/* Pending bits, read-only (bit n = 1 means IRQ n is pending) */
#define PLIC_PENDING_REG(n)  (*(volatile uint32_t*)(PLIC_BASE + 0x1000 + ((n) / 32) * 4))

/* Enable bits for hart 0, M-mode (context 0) */
#define PLIC_ENABLE_REG(n)   (*(volatile uint32_t*)(PLIC_BASE + 0x2000 + ((n) / 32) * 4))

/* Threshold and claim/complete for hart 0, M-mode (context 0) */
#define PLIC_THRESHOLD  (*(volatile uint32_t*)(PLIC_BASE + 0x200000))
#define PLIC_CLAIM      (*(volatile uint32_t*)(PLIC_BASE + 0x200004))

/* Enable IRQ source n with given priority (1–7) for hart 0 M-mode.
 * Sets threshold to 0 so all priorities are delivered.
 * Caller must also set mie.MEIE and mstatus.MIE (done in start.S). */
void plic_enable(int irq, int priority);

/* Claim the highest-priority pending IRQ. Returns 0 if none. */
static inline uint32_t plic_claim(void) { return PLIC_CLAIM; }

/* Signal completion for a previously claimed IRQ. */
static inline void plic_complete(uint32_t irq) { PLIC_CLAIM = irq; }

#endif /* PLIC_H */
