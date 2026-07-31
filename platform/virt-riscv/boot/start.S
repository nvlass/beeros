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

        /* Enable FPU: mstatus.FS = 01 (Initial) — gcc uses fld/fsd to copy structs */
        li      t0, 0x2000
        csrs    mstatus, t0

        /* Thread pointer — required for picolibc TLS (errno, etc.) */
        la      tp, __tls_base

        /* Clear .tbss (TLS zero-initialized data) */
        la      t0, __tbss_start
        la      t1, __tbss_end
.Ltbss: bgeu    t0, t1, .Lbss_init
        sd      zero, 0(t0)
        addi    t0, t0, 8
        j       .Ltbss

        /* Clear .bss */
.Lbss_init:
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
