/*
 * memtest_kernel.S - full-RDRAM read/write test kernel
 *
 * This routine is copied into RSP DMEM (0xA4000000) at run time and executed
 * from there, so it can test *every* byte of RDRAM, including the memory the
 * C program, its heap, stack and framebuffers occupy.  It is non-destructive:
 * RDRAM is processed in 4 KiB chunks; each chunk is saved into RSP IMEM,
 * tested with six patterns, then restored.  Only 32-bit loads/stores are
 * used (SP memory does not support 64-bit access), no stack is used, and
 * only caller-saved registers are touched, so it can be called from C with
 * a plain function pointer (interrupts must be disabled by the caller).
 *
 * Inputs (all KSEG1 / uncached addresses):
 *   a0 = first RDRAM address to test      (0xA0000000)
 *   a1 = size in bytes, multiple of 4096  (4 or 8 MiB)
 *   a2 = 4 KiB chunk save buffer          (0xA4001000, IMEM)
 *   a3 = result block in DMEM             (0xA4000F00), zeroed by caller:
 *          0x00  total error count
 *          0x04  first bad address (0 = none)
 *          0x08  first bad: expected value
 *          0x0C  first bad: value read
 *          0x10  current chunk address (progress / hang diagnosis)
 *          0x14  saved return address
 *          0x20..0x3C  error count per MiB (8 words)
 *
 * Patterns: 00000000, FFFFFFFF, AAAAAAAA, 55555555, ADDR, ~ADDR
 */

#define CHUNK 4096

    .set noreorder
    .set noat

    .section .text.memtest_kernel, "ax", @progbits
    .align 3
    .globl __memtest_kernel_start
    .globl __memtest_kernel_end

/* Verify one word at \off(t2) against t4 (expected).  On mismatch bump the
 * counters and remember the first failure. Clobbers t3, t5, t8, v1
 * (t9 is the loop end pointer of the caller and must survive). */
.macro CHECK off
    lw    $t5, \off($t2)
    beq   $t5, $t4, 9f
    nop
    addiu $t6, $t6, 1               /* total errors++ */
    addiu $t3, $t2, \off            /* bad address */
    subu  $v1, $t3, $a0
    srl   $v1, $v1, 20
    sll   $v1, $v1, 2
    addu  $v1, $v1, $a3
    lw    $t8, 0x20($v1)            /* per-MiB counter++ */
    addiu $t8, $t8, 1
    sw    $t8, 0x20($v1)
    bnez  $t7, 9f
    nop
    move  $t7, $t3                  /* first failure */
    sw    $t3, 0x04($a3)
    sw    $t4, 0x08($a3)
    sw    $t5, 0x0C($a3)
9:
.endm

__memtest_kernel_start:
    sll   $a0, $a0, 0               /* make sure the 32-bit args are   */
    sll   $a1, $a1, 0               /* sign-extended (lw results are)  */
    sll   $a2, $a2, 0
    sll   $a3, $a3, 0
    sw    $ra, 0x14($a3)
    move  $t6, $zero                /* error count */
    move  $t7, $zero                /* first bad address */
    move  $t0, $a0                  /* current chunk */
    addu  $t1, $a0, $a1             /* end */

chunk_loop:
    sw    $t0, 0x10($a3)            /* progress */

    /* ---- save chunk -> IMEM buffer ---- */
    move  $t2, $t0
    move  $t3, $a2
    addiu $t9, $t0, CHUNK
save_loop:
    lw    $t4, 0($t2)
    lw    $t5, 4($t2)
    lw    $t8, 8($t2)
    lw    $v1, 12($t2)
    addiu $t2, $t2, 16
    sw    $t4, 0($t3)
    sw    $t5, 4($t3)
    sw    $t8, 8($t3)
    sw    $v1, 12($t3)
    bne   $t2, $t9, save_loop
    addiu $t3, $t3, 16

    /* ---- constant patterns (li may expand to 2 insns: keep it out of
            the delay slot) ---- */
    move  $v0, $zero
    bal   fv_const
    nop
    li    $v0, 0xFFFFFFFF
    bal   fv_const
    nop
    li    $v0, 0xAAAAAAAA
    bal   fv_const
    nop
    li    $v0, 0x55555555
    bal   fv_const
    nop
    /* ---- address patterns: value = addr ^ v0 ---- */
    move  $v0, $zero
    bal   fv_addr
    nop
    li    $v0, 0xFFFFFFFF
    bal   fv_addr
    nop

    /* ---- restore chunk <- IMEM buffer ---- */
    move  $t2, $t0
    move  $t3, $a2
    addiu $t9, $t0, CHUNK
restore_loop:
    lw    $t4, 0($t3)
    lw    $t5, 4($t3)
    lw    $t8, 8($t3)
    lw    $v1, 12($t3)
    addiu $t3, $t3, 16
    sw    $t4, 0($t2)
    sw    $t5, 4($t2)
    sw    $t8, 8($t2)
    sw    $v1, 12($t2)
    addiu $t2, $t2, 16
    bne   $t2, $t9, restore_loop
    nop

    addiu $t0, $t0, CHUNK
    bne   $t0, $t1, chunk_loop
    nop

    sw    $t6, 0x00($a3)
    lw    $ra, 0x14($a3)
    nop
    jr    $ra
    nop

/* fill chunk [t0, t0+CHUNK) with constant v0, then verify */
fv_const:
    move  $t2, $t0
    addiu $t9, $t0, CHUNK
1:  sw    $v0, 0($t2)
    sw    $v0, 4($t2)
    sw    $v0, 8($t2)
    sw    $v0, 12($t2)
    addiu $t2, $t2, 16
    bne   $t2, $t9, 1b
    nop
    move  $t2, $t0
    move  $t4, $v0
2:  CHECK 0
    CHECK 4
    CHECK 8
    CHECK 12
    addiu $t2, $t2, 16
    bne   $t2, $t9, 2b
    nop
    jr    $ra
    nop

/* fill chunk with (address ^ v0), then verify */
fv_addr:
    move  $t2, $t0
    addiu $t9, $t0, CHUNK
1:  xor   $t4, $t2, $v0
    sw    $t4, 0($t2)
    addiu $t5, $t2, 4
    xor   $t5, $t5, $v0
    sw    $t5, 4($t2)
    addiu $t8, $t2, 8
    xor   $t8, $t8, $v0
    sw    $t8, 8($t2)
    addiu $v1, $t2, 12
    xor   $v1, $v1, $v0
    sw    $v1, 12($t2)
    addiu $t2, $t2, 16
    bne   $t2, $t9, 1b
    nop
    move  $t2, $t0
2:  xor   $t4, $t2, $v0
    CHECK 0
    addiu $t4, $t2, 4
    xor   $t4, $t4, $v0
    CHECK 4
    addiu $t4, $t2, 8
    xor   $t4, $t4, $v0
    CHECK 8
    addiu $t4, $t2, 12
    xor   $t4, $t4, $v0
    CHECK 12
    addiu $t2, $t2, 16
    bne   $t2, $t9, 2b
    nop
    jr    $ra
    nop

    .align 3
__memtest_kernel_end:
