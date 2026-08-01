/* trap.S — M-mode trap handler for RISC-V virt
 *
 * Saves all 31 GPRs plus mepc and mcause into a static TrapFrame in .bss,
 * calls mem_trap_handler() to claim the PLIC IRQ and set irq_pending, then
 * restores all registers and returns via mret.
 *
 * mtvec is pointed here by start.S before calling beeros_main().
 * Direct mode (mtvec[1:0] = 0): all traps use this single entry point.
 *
 * IMPORTANT: t0 is used to address the frame before we can save anything.
 * We save t0 last by using a temporary CSR (mscratch) to stash the
 * original t0 value first.
 */

        .section .text
        .global  trap_vector
        .align   2          /* mtvec requires 4-byte alignment */

trap_vector:
        /* Stash t0 in mscratch so we can use it to address the frame */
        csrw    mscratch, t0
        la      t0, g_trap_frame

        sd      ra,   0(t0)
        sd      sp,   8(t0)
        sd      gp,  16(t0)
        sd      tp,  24(t0)
        /* t0 saved below after restoring from mscratch */
        sd      t1,  40(t0)
        sd      t2,  48(t0)
        sd      s0,  56(t0)
        sd      s1,  64(t0)
        sd      a0,  72(t0)
        sd      a1,  80(t0)
        sd      a2,  88(t0)
        sd      a3,  96(t0)
        sd      a4, 104(t0)
        sd      a5, 112(t0)
        sd      a6, 120(t0)
        sd      a7, 128(t0)
        sd      s2, 136(t0)
        sd      s3, 144(t0)
        sd      s4, 152(t0)
        sd      s5, 160(t0)
        sd      s6, 168(t0)
        sd      s7, 176(t0)
        sd      s8, 184(t0)
        sd      s9, 192(t0)
        sd      s10,200(t0)
        sd      s11,208(t0)
        sd      t3, 216(t0)
        sd      t4, 224(t0)
        sd      t5, 232(t0)
        sd      t6, 240(t0)

        /* Save original t0 (now in mscratch) */
        csrr    t1, mscratch
        sd      t1, 32(t0)

        /* Save mepc and mcause */
        csrr    t1, mepc
        sd      t1, 248(t0)
        csrr    t1, mcause
        sd      t1, 256(t0)

        /* Set up a minimal stack frame for C (use the saved sp slot) */
        ld      sp, 8(t0)          /* restore sp so C has a valid stack */
        addi    sp, sp, -16        /* minimal frame for ABI compliance */

        call    mem_trap_handler

        addi    sp, sp, 16

        /* Restore all registers from frame */
        la      t0, g_trap_frame

        ld      ra,   0(t0)
        ld      sp,   8(t0)
        ld      gp,  16(t0)
        ld      tp,  24(t0)
        /* t0 restored last */
        ld      t1,  40(t0)
        ld      t2,  48(t0)
        ld      s0,  56(t0)
        ld      s1,  64(t0)
        ld      a0,  72(t0)
        ld      a1,  80(t0)
        ld      a2,  88(t0)
        ld      a3,  96(t0)
        ld      a4, 104(t0)
        ld      a5, 112(t0)
        ld      a6, 120(t0)
        ld      a7, 128(t0)
        ld      s2, 136(t0)
        ld      s3, 144(t0)
        ld      s4, 152(t0)
        ld      s5, 160(t0)
        ld      s6, 168(t0)
        ld      s7, 176(t0)
        ld      s8, 184(t0)
        ld      s9, 192(t0)
        ld      s10,200(t0)
        ld      s11,208(t0)
        ld      t3, 216(t0)
        ld      t4, 224(t0)
        ld      t5, 232(t0)
        ld      t6, 240(t0)

        /* Restore mepc */
        ld      t0, 248(t0)
        csrw    mepc, t0

        /* Restore t0 */
        la      t0, g_trap_frame
        ld      t0, 32(t0)

        mret

        /* TrapFrame: 31 GPRs (x1–x31) × 8 bytes + mepc + mcause = 264 bytes */
        .section .bss
        .align   3
        .global  g_trap_frame
g_trap_frame:
        .space   264
