/* start.S — AArch64 bare-metal entry for qemu-system-aarch64 -M virt
 *
 * QEMU -M virt loads the ELF at its link address and jumps to the entry
 * point at EL1.  Secondary CPUs are parked via MPIDR_EL1 check.
 */

        .section ".text.boot", "ax"
        .global  _start
_start:
        /* Park every core except core 0 (Aff0 field of MPIDR_EL1) */
        mrs     x0, mpidr_el1
        and     x0, x0, #0xFF
        cbnz    x0, .Lpark

        /* Stack */
        ldr     x0, =_stack_top
        mov     sp, x0

        /* Clear BSS */
        ldr     x0, =_bss_start
        ldr     x1, =_bss_end
.Lbss:  cmp     x0, x1
        bge     .Lcall
        str     xzr, [x0], #8
        b       .Lbss

.Lcall: bl      beeros_main

        /* beeros_main should never return; halt if it does */
.Lhalt: wfe
        b       .Lhalt

.Lpark: wfe
        b       .Lpark
