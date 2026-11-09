###################################################################
# rsp_xbus.S (rdp_timing v41): feed an RDP display list to the RDP through the XBUS (DMEM), the way
# the graphics microcodes do, instead of letting the RDP fetch it from RDRAM.
#
# DMEM 0x000: parameters written by the CPU: RDRAM address of the chunked list, number of chunks,
# chunk size in bytes (<= 1024, a multiple of 8). The list is pre-split by the CPU into chunks of exactly
# that size, each ending on a command boundary (padded with No_Op). Two DMEM buffers (0x400 and 0xC00)
# are used alternately: chunk i is DMAed into buffer i&1 once the RDP has started chunk i-1 (DP_START
# no longer pending), then DP_START/DP_END are written for it (the RDP takes the pair when it reaches
# the end of chunk i-1).
###################################################################
#include <rsp.inc>

    .data
    .align 3
PARAMS:
PARAM_RDRAM:  .long 0      # source list in RDRAM (chunked)
PARAM_N:      .long 0      # number of chunks
PARAM_CHUNK:  .long 0      # chunk size in bytes
PARAM_MODE:   .long 0      # 0: XBUS (the RDP reads DMEM), 1: FIFO (each chunk is DMAed on to PARAM_DEST in RDRAM and the RDP reads it from there, as the fifo microcodes do)
PARAM_DEST:   .long 0
PARAM_PAD:    .long 0, 0, 0

    .text
    .globl _start
_start:
    lw s0, %lo(PARAM_RDRAM)(zero)
    lw t4, %lo(PARAM_N)(zero)
    lw t5, %lo(PARAM_CHUNK)(zero)
    li s1, 0x400
    li s2, 0xC00
    beqz t4, done
    nop
    lw t6, %lo(PARAM_MODE)(zero)
    lw s3, %lo(PARAM_DEST)(zero)
    bnez t6, fifo
    nop
    # chunk 0: DMA, then start the RDP on it
    jal dma_in
    nop
    jal dma_wait
    nop
    mtc0 s1, COP0_DP_START
    add t0, s1, t5
    mtc0 t0, COP0_DP_END
    add s0, s0, t5
    addi t4, t4, -1
loop:
    beqz t4, done
    nop
    move t0, s1
    move s1, s2
    move s2, t0
wait_sv:
    # the previous DP_START must have been taken by the RDP (it is then running the other buffer,
    # and the buffer we are about to fill, used two chunks ago, is free)
    mfc0 t0, COP0_DP_STATUS
    andi t0, t0, DP_STATUS_START_VALID
    bnez t0, wait_sv
    nop
    jal dma_in
    nop
    jal dma_wait
    nop
    mtc0 s1, COP0_DP_START
    add t0, s1, t5
    mtc0 t0, COP0_DP_END
    add s0, s0, t5
    j loop
    addi t4, t4, -1
done:
    break
    nop

    # FIFO mode: chunk i: RDRAM src -> DMEM 0x400 -> RDRAM dest+i*chunk, then DP_END = end of that chunk
    # (DP_START = dest before the first); the RDP fetches from RDRAM what the RSP has just written, a chunk behind
fifo:
    mtc0 s3, COP0_DP_START
    move s4, s3                 # write pointer in dest
fifo_loop:
    beqz t4, done
    nop
    jal dma_in
    nop
    jal dma_wait
    nop
    jal dma_out                 # DMEM s1 -> RDRAM s4, t5 bytes
    nop
    jal dma_wait
    nop
    add s4, s4, t5
    mtc0 s4, COP0_DP_END
    add s0, s0, t5
    j fifo_loop
    addi t4, t4, -1

dma_out:
    mfc0 t0, COP0_DMA_FULL
    bnez t0, dma_out
    nop
    mtc0 s1, COP0_DMA_SPADDR
    mtc0 s4, COP0_DMA_RAMADDR
    addi t0, t5, -1
    jr ra
    mtc0 t0, COP0_DMA_WRITE

# dma_in: RDRAM s0 -> DMEM s1, t5 bytes
dma_in:
    mfc0 t0, COP0_DMA_FULL
    bnez t0, dma_in
    nop
    mtc0 s1, COP0_DMA_SPADDR
    mtc0 s0, COP0_DMA_RAMADDR
    addi t0, t5, -1
    jr ra
    mtc0 t0, COP0_DMA_READ

dma_wait:
    mfc0 t0, COP0_DMA_BUSY
    bnez t0, dma_wait
    nop
    jr ra
    nop
