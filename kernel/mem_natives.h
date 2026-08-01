#ifndef MEM_NATIVES_H
#define MEM_NATIVES_H

/* Register the beer.mem namespace and its 16 native primitives. */
void mem_register_natives(void);

/* Compile and run the embedded lib/mem.beer source (rmw32!, poll-set/clear32,
 * irq-listen!, and named MMIO address constants). */
void mem_load_library(void);

/* Called from platform/virt-riscv/hal/trap.S after registers are saved:
 * claims the PLIC IRQ, sets the irq_pending bit, completes the claim. */
void mem_trap_handler(void);

#endif /* MEM_NATIVES_H */
