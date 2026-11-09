/* mgc_tasklog: resident timing logger for the MGC 2011 demo (Retroactive), adapted from dk64_tasklog / bar_tasklog
 *
 * Loaded by the patched entry point to LOGGER_BASE (a region the demo never touches, see README); the log buffer
 * (96 KiB) follows it. Hooks the demo's libultra (boot segment, ROM 0x1000 -> RAM 0x80001000) and logs, with the
 * COP0 Count:
 *   osSpTaskLoad(tp)  0x80015010   kind 1, payload = task type << 8 | task flags (graphics tasks only)
 *   osSpTaskStartGo   0x8001521c   kind 2 (the SP_STATUS write that clears HALT; graphics tasks only)
 *   send_mesg(ev*8)   0x8000dffc   kind 3, payload = event << 8 | extra (SP: SP_STATUS bits + task type, DP: DPC_STATUS, VI: line/2)
 *   osSpTaskYield     0x80015660   kind 4 (the SP_STATUS write that sets SIG0)
 * Entry: bits 31..16 = Count delta since the previous entry / 16 (saturating), 15..12 kind, 11..0 payload.
 * Logging starts at the first graphics task that comes 3.8 VI or more after the previous one's go, after the
 * MIN_VI-th VI (= the loading gap in front of part 4, the speaker room; the gaps in front of parts 2 and 3 come
 * earlier). When the 96 KiB buffer is full, or at the END_VI-th VI, the buffer is written to the cartridge SRAM
 * (0x08000000, three 32 KiB chunks, the first chunk last so that a 32 KiB SRAM keeps the beginning) and the
 * console hangs.
 */

/* LOGGER_BASE = 0x807E6000: code 0x7E6000, state 0x7E7000, log buffer 0x7E8000-0x7FFFFF (96 KiB); the demo never
 * touches 0x7E6000-0x7FFFFF (its stack is at 0x3FFFF0, the last colour buffer ends at 0x796000) */
#define STATE_HI    0xa07e
#define STATE_LO    0x7000
#define LOGBUF_HI   0xa07e
#define LOGBUF_LO   0x8000
#define LOGPHYS_HI  0x007e
#define LOGPHYS_LO  0x8000
#define LOGBYTES_HI 0x0001
#define LOGBYTES_LO 0x8000
#define ST_ENABLED  0
#define ST_WPTR     4
#define ST_LAST     8
#define ST_VICOUNT  12
#define ST_SAVE6    16
#define ST_SAVE7    20
#define ST_SAVE8    24
#define ST_CURTYPE  28
#define ST_GO       32
#define ST_DUMPED   36
#define MIN_VI      3000
#define END_VI      10500
#define GAP_TICKS   2968750       /* 3.8 VI: the loading gap in front of part 4 is 5 VI, the longest frame of part 3 2.7 VI */

#define SP_STATUS   0xA4040010
#define DPC_STATUS  0xA410000C
#define VI_CURRENT  0xA4400010

    .set noreorder
    .set noat
    .section .text
    .globl _start
_start:
init:
    /* original entry (0x80001000): sp = 0x803FFFF0, clear 0x8004CF90 .. 0x80198510, jal 0x800032c0 (returns to 0x80001038: break) */
    lui     $sp, 0x803f
    ori     $sp, $sp, 0xfff0
    lui     $t0, 0x8005
    addiu   $t0, $t0, -12400
    lui     $t1, 0x8019
    addiu   $t1, $t1, -31472
    beq     $t0, $t1, 2f
    nop
1:  addiu   $t0, $t0, 4
    sltu    $at, $t0, $t1
    bnez    $at, 1b
    sw      $zero, -4($t0)
2:
    /* state */
    lui     $t0, STATE_HI
    ori     $t0, $t0, STATE_LO
    sw      $zero, ST_ENABLED($t0)
    sw      $zero, ST_WPTR($t0)
    sw      $zero, ST_VICOUNT($t0)
    sw      $zero, ST_CURTYPE($t0)
    sw      $zero, ST_DUMPED($t0)
    sw      $zero, ST_GO($t0)

    /* hooks: j/jal into this blob (t1 = 0x80010000, offsets from there) */
    lui     $t1, 0x8001
    /* osSpTaskLoad 0x80015010: j hookA ; nop (replaced: addiu sp,sp,-32 ; sw s0,16(sp)) */
    la      $t2, hookA
    srl     $t2, $t2, 2
    lui     $t3, 0x0fff
    ori     $t3, $t3, 0xffff
    and     $t2, $t2, $t3
    lui     $t3, 0x0800
    or      $t2, $t2, $t3
    sw      $t2, 0x5010($t1)
    sw      $zero, 0x5014($t1)
    cache   0x19, 0x5010($t1)
    cache   0x10, 0x5010($t1)
    /* send_mesg 0x8000dffc: j hookC ; nop (replaced: move s2,ra ; lui t2,0x8018) */
    la      $t2, hookC
    srl     $t2, $t2, 2
    lui     $t3, 0x0fff
    ori     $t3, $t3, 0xffff
    and     $t2, $t2, $t3
    lui     $t3, 0x0800
    or      $t2, $t2, $t3
    sw      $t2, -0x2004($t1)
    sw      $zero, -0x2000($t1)
    cache   0x19, -0x2004($t1)
    cache   0x10, -0x2004($t1)
    /* osSpTaskStartGo 0x80015234: jal hookB (a0 = 0x125 in the delay slot) */
    la      $t2, hookB
    srl     $t2, $t2, 2
    lui     $t3, 0x0fff
    ori     $t3, $t3, 0xffff
    and     $t2, $t2, $t3
    lui     $t3, 0x0c00
    or      $t2, $t2, $t3
    sw      $t2, 0x5234($t1)
    cache   0x19, 0x5234($t1)
    cache   0x10, 0x5234($t1)
    /* osSpTaskYield 0x80015668: jal hookD (a0 = 0x400) */
    la      $t2, hookD
    srl     $t2, $t2, 2
    lui     $t3, 0x0fff
    ori     $t3, $t3, 0xffff
    and     $t2, $t2, $t3
    lui     $t3, 0x0c00
    or      $t2, $t2, $t3
    sw      $t2, 0x5668($t1)
    cache   0x19, 0x5668($t1)
    cache   0x10, 0x5668($t1)

    /* continue the original entry: jal 0x800032c0 with the original return address */
    lui     $ra, 0x8000
    ori     $ra, $ra, 0x1038
    lui     $t2, 0x8000
    ori     $t2, $t2, 0x32c0
    jr      $t2
    nop

/* ---- log_entry: $t0 = kind<<12 | payload; clobbers t2-t7; ra preserved by the caller in t8 ---- */
log_entry:
    lui     $t2, STATE_HI
    ori     $t2, $t2, STATE_LO
    lw      $t3, ST_ENABLED($t2)
    nop
    beqz    $t3, 9f
    nop
    mfc0    $t4, $9
    lw      $t5, ST_LAST($t2)
    sw      $t4, ST_LAST($t2)
    subu    $t5, $t4, $t5
    srl     $t5, $t5, 4
    lui     $t6, 1
    sltu    $t7, $t5, $t6
    bnez    $t7, 1f
    nop
    ori     $t5, $zero, 0xffff
1:  sll     $t5, $t5, 16
    or      $t5, $t5, $t0
    lw      $t6, ST_WPTR($t2)
    lui     $t7, LOGBUF_HI
    ori     $t7, $t7, LOGBUF_LO
    addu    $t7, $t7, $t6
    sw      $t5, 0($t7)
    addiu   $t6, $t6, 4
    sw      $t6, ST_WPTR($t2)
    lui     $t7, LOGBYTES_HI
    ori     $t7, $t7, LOGBYTES_LO
    bne     $t6, $t7, 9f
    nop
    j       dump
    nop
9:  jr      $ra
    nop

/* ---- hookA: osSpTaskLoad entry (replaced: addiu sp,sp,-32 ; sw s0,16(sp)); the function saves ra at 24(sp) itself later ---- */
hookA:
    addiu   $sp, $sp, -32
    sw      $s0, 16($sp)
    sw      $ra, 24($sp)
    lw      $t0, 0($a0)         /* type */
    lw      $t1, 4($a0)         /* flags */
    lui     $t2, STATE_HI
    ori     $t2, $t2, STATE_LO
    sw      $t0, ST_CURTYPE($t2)
    ori     $t3, $zero, 1
    bne     $t0, $t3, 1f
    nop
    /* trigger: a fresh graphics task 7 VI or more after the previous one's go, after MIN_VI */
    lw      $t3, ST_ENABLED($t2)
    nop
    bnez    $t3, 2f
    andi    $t3, $t1, 1
    bnez    $t3, 2f             /* a resumed (yielded) task: not a fresh one */
    nop
    lw      $t3, ST_VICOUNT($t2)
    ori     $t4, $zero, MIN_VI
    sltu    $t4, $t3, $t4
    bnez    $t4, 2f
    nop
    mfc0    $t4, $9
    lw      $t5, ST_GO($t2)
    nop
    subu    $t4, $t4, $t5
    lui     $t5, %hi(GAP_TICKS)
    ori     $t5, $t5, %lo(GAP_TICKS)
    sltu    $t5, $t4, $t5
    bnez    $t5, 2f
    nop
    jal     enable_log
    nop
2:  sll     $t0, $t0, 8
    andi    $t1, $t1, 0xff
    or      $t0, $t0, $t1
    andi    $t0, $t0, 0x0fff
    ori     $t0, $t0, 0x1000
    jal     log_entry
    nop
1:  lw      $ra, 24($sp)
    lui     $t0, 0x8001
    ori     $t0, $t0, 0x5018
    jr      $t0
    nop

/* ---- enable_log: header = magic, VI count, Count, version; clobbers t2,t4,t6,t7 ---- */
enable_log:
    lui     $t2, STATE_HI
    ori     $t2, $t2, STATE_LO
    lw      $t4, ST_VICOUNT($t2)
    lui     $t6, LOGBUF_HI
    ori     $t6, $t6, LOGBUF_LO
    lui     $t7, 0x4d47
    ori     $t7, $t7, 0x544c      /* "MGTL" */
    sw      $t7, 0($t6)
    sw      $t4, 4($t6)
    mfc0    $t7, $9
    sw      $t7, 8($t6)
    ori     $t7, $zero, 7         /* v7: MGC, no PI entries, 96 KiB buffer */
    sw      $t7, 12($t6)
    mfc0    $t7, $9
    sw      $t7, ST_LAST($t2)
    ori     $t7, $zero, 16
    sw      $t7, ST_WPTR($t2)
    ori     $t7, $zero, 1
    sw      $t7, ST_ENABLED($t2)
    jr      $ra
    nop

/* ---- hookB: replaces the jal __osSpSetStatus in osSpTaskStartGo (a0 = 0x125) ---- */
hookB:
    move    $t8, $ra
    lui     $t2, STATE_HI
    ori     $t2, $t2, STATE_LO
    lw      $t3, ST_CURTYPE($t2)
    ori     $t4, $zero, 1
    bne     $t3, $t4, 1f
    nop
    mfc0    $t4, $9
    sw      $t4, ST_GO($t2)
    ori     $t0, $zero, 0x2000
    jal     log_entry
    nop
1:  move    $ra, $t8
    lui     $t6, 0xa404
    jr      $ra
    sw      $a0, 0x10($t6)

/* ---- hookD: replaces the jal __osSpSetStatus in osSpTaskYield (a0 = 0x400) ---- */
hookD:
    move    $t8, $ra
    ori     $t0, $zero, 0x4000
    jal     log_entry
    nop
    move    $ra, $t8
    lui     $t6, 0xa404
    jr      $ra
    sw      $a0, 0x10($t6)

/* ---- hookC: send_mesg entry in the exception handler (a0 = event*8); replaced: move s2,ra ; lui t2,0x8018 ---- */
hookC:
    move    $s2, $ra
    lui     $t2, STATE_HI
    ori     $t2, $t2, STATE_LO
    sw      $t6, ST_SAVE6($t2)
    sw      $t7, ST_SAVE7($t2)
    sw      $t8, ST_SAVE8($t2)
    srl     $t0, $a0, 3         /* event */
    sll     $t0, $t0, 8
    ori     $t0, $t0, 0x3000
    /* extra byte per event */
    ori     $t3, $zero, 0x20
    bne     $a0, $t3, 2f
    ori     $t3, $zero, 0x48
    lui     $t4, %hi(SP_STATUS)
    lw      $t4, %lo(SP_STATUS)($t4)
    nop
    andi    $t5, $t4, 0x3       /* halt, broke */
    srl     $t4, $t4, 5
    andi    $t4, $t4, 0x1c      /* sig0 (yield), sig1 (yielded), sig2 (task done) */
    or      $t5, $t5, $t4
    lw      $t4, ST_CURTYPE($t2)
    nop
    andi    $t4, $t4, 0x3
    sll     $t4, $t4, 5
    or      $t5, $t5, $t4
    b       5f
    or      $t0, $t0, $t5
2:  bne     $a0, $t3, 3f
    ori     $t3, $zero, 0x38
    lui     $t4, %hi(DPC_STATUS)
    lw      $t4, %lo(DPC_STATUS)($t4)
    nop
    andi    $t4, $t4, 0xff
    b       5f
    or      $t0, $t0, $t4
3:  bne     $a0, $t3, 8f
    nop
    /* VI: count frames; at END_VI dump whatever was logged */
    lw      $t4, ST_VICOUNT($t2)
    lui     $t5, %hi(VI_CURRENT)
    lw      $t5, %lo(VI_CURRENT)($t5)
    addiu   $t4, $t4, 1
    sw      $t4, ST_VICOUNT($t2)
    srl     $t5, $t5, 1
    andi    $t5, $t5, 0xff
    or      $t0, $t0, $t5
    ori     $t5, $zero, END_VI
    bne     $t4, $t5, 5f
    nop
    j       dump
    nop
8:  ori     $t3, $zero, 0x40
    beq     $a0, $t3, 5f        /* PI: logged (extra 0) */
    nop
    b       7f                  /* any other event: not logged */
    nop
5:  jal     log_entry
    nop
7:  lui     $t2, STATE_HI
    ori     $t2, $t2, STATE_LO
    lw      $t6, ST_SAVE6($t2)
    lw      $t7, ST_SAVE7($t2)
    lw      $t8, ST_SAVE8($t2)
    move    $ra, $s2
    lui     $t2, 0x8018
    lui     $t3, 0x8000
    ori     $t3, $t3, 0xe004
    jr      $t3
    nop

/* ---- dump: write the buffer to SRAM (3 x 32 KiB, last chunk first) and hang ---- */
dump:
    mfc0    $t0, $12
    li      $t1, ~1
    and     $t0, $t0, $t1
    mtc0    $t0, $12            /* interrupts off */
    nop
    nop
    lui     $t0, 0xa460
    /* domain 2 timing for SRAM */
    ori     $t1, $zero, 5
    sw      $t1, 0x24($t0)
    ori     $t1, $zero, 0x0c
    sw      $t1, 0x28($t0)
    ori     $t1, $zero, 0x0d
    sw      $t1, 0x2c($t0)
    ori     $t1, $zero, 2
    sw      $t1, 0x30($t0)
    ori     $t4, $zero, 2       /* three 32 KiB chunks */
1:  lw      $t1, 0x10($t0)
    nop
    andi    $t1, $t1, 3
    bnez    $t1, 1b
    nop
    sll     $t5, $t4, 15        /* chunk * 32 KiB */
    lui     $t1, LOGPHYS_HI
    ori     $t1, $t1, LOGPHYS_LO
    addu    $t1, $t1, $t5
    sw      $t1, 0x00($t0)      /* PI_DRAM_ADDR */
    lui     $t1, 0x0800
    addu    $t1, $t1, $t5
    sw      $t1, 0x04($t0)      /* PI_CART_ADDR */
    ori     $t1, $zero, 0x7fff
    sw      $t1, 0x08($t0)      /* PI_RD_LEN: RDRAM -> cart */
2:  lw      $t1, 0x10($t0)
    nop
    andi    $t1, $t1, 3
    bnez    $t1, 2b
    nop
    bnez    $t4, 1b
    addiu   $t4, $t4, -1
    /* done marker for an emulator watch */
    lui     $t2, STATE_HI
    ori     $t2, $t2, STATE_LO
    lui     $t3, 0xd0d0
    sw      $t3, ST_ENABLED($t2)
3:  b       3b
    nop
