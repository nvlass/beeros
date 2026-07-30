/* start.S — RISC-V bare-metal entry for qemu-system-riscv64 -M virt
 *
 * Reset vector: 0x80000000 (DRAM base on the virt machine).
 * Secondary harts are parked here; only hart 0 initialises the system.
 */

        .section .text.boot, "ax"
        .global  _start
_start:
        /* Park every hart except hart 0 */
        csrr    t0, mhartid
        bnez    t0, .Lpark

        /* Stack */
        la      sp, _stack_top

        /* Clear BSS */
        la      t0, _bss_start
        la      t1, _bss_end
.Lbss:  bgeu    t0, t1, .Lcall
        sd      zero, 0(t0)
        addi    t0, t0, 8
        j       .Lbss

.Lcall: call    beeros_main

        /* beeros_main should never return; halt if it does */
.Lhalt: wfi
        j       .Lhalt

.Lpark: wfi
        j       .Lpark
