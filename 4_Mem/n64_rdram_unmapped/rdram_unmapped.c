/*
 * rdram_unmapped - N64 test ROM: reads from the unpopulated part of the
 * RDRAM address space (end of installed RAM .. 0x03EFFFFF)
 *
 * Purpose: verify on real hardware what the CPU (and the RSP DMA) gets back
 * when reading RDRAM addresses that have no RDRAM device behind them, for
 * every load width (lb/lbu/lh/lhu/lw/lwu/ld), uncached and cached, with and
 * without a preceding load of a known non-zero pattern (to expose any
 * "open bus" behaviour).  Expected per the emulator change under test:
 * every access width returns 0.
 *
 * 0x03F00000-0x03FFFFFF (RDRAM registers) is NOT touched.
 * The report is printed on screen, saved to SD as a new numbered text file
 * (never overwritten), and the same text is sent line by line to the USB
 * log and to the ISViewer (emulator) log, so the full result can also be
 * read on an emulator without SD card support.
 *
 * Built with libdragon.
 * Author: Nekokabu <nekokabu@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <libdragon.h>
#include <libcart/cart.h>

#define VERSION        "1.2.1"
#define OUT_DIR        "sd:/n64_memspeed"
#define OUT_NAME       "rdram_unmapped_v12_%03d.txt"

#define MiB            (1024u * 1024u)
#define RDRAM_SPACE_END 0x03F00000u          /* exclusive; RDRAM regs above */
#define KSEG0(p)       (0x80000000u | (p))
#define KSEG1(p)       (0xA0000000u | (p))

/* ------------------------------------------------------------------------ */
/* report output                                                             */
/* ------------------------------------------------------------------------ */

static char report[96 * 1024];
static int  report_len = 0;
static bool quiet = false;          /* true: file/USB/ISViewer only, no screen */
static bool log_usb = false, log_isv = false;   /* debug channels found at boot */

static void outf(const char *fmt, ...)
{
    char line[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof(line)) n = sizeof(line) - 1;
    if (report_len + n < (int)sizeof(report)) {
        memcpy(report + report_len, line, n);
        report_len += n;
        report[report_len] = 0;
    }
    debugf("%s", line);
    if (!quiet) { printf("%s", line); console_render(); }
}

/* progress line on screen only (helps to see where it stopped if it hangs) */
static void progress(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    console_render();
}

/* ------------------------------------------------------------------------ */
/* exact single-instruction loads (inline asm so the compiler cannot merge,  */
/* widen or reorder them).  The result is the full 64-bit GPR value.         */
/* ------------------------------------------------------------------------ */

#define LOADFN(name, insn)                                                   \
static inline uint64_t name(uint32_t a)                                      \
{                                                                            \
    uint64_t v;                                                              \
    __asm__ volatile(insn " %0, 0(%1)" : "=r"(v) : "r"((int32_t)a) : "memory"); \
    return v;                                                                \
}                                                                            \
/* same load, immediately preceded by an uncached ld of a known pattern */   \
static inline uint64_t name##_p(uint32_t a, uint32_t pa)                     \
{                                                                            \
    uint64_t v, d;                                                           \
    __asm__ volatile("ld %1, 0(%3)\n\t" insn " %0, 0(%2)"                    \
                     : "=&r"(v), "=&r"(d) : "r"((int32_t)a), "r"((int32_t)pa) \
                     : "memory");                                            \
    (void)d;                                                                 \
    return v;                                                                \
}

LOADFN(ld_lb,  "lb")
LOADFN(ld_lbu, "lbu")
LOADFN(ld_lh,  "lh")
LOADFN(ld_lhu, "lhu")
LOADFN(ld_lw,  "lw")
LOADFN(ld_lwu, "lwu")
LOADFN(ld_ld,  "ld")

typedef uint64_t (*load_fn)(uint32_t);
typedef uint64_t (*load_p_fn)(uint32_t, uint32_t);

typedef struct {
    const char *name;
    load_fn     f;
    load_p_fn   fp;
    int         size;     /* bytes */
    bool        sign;     /* sign-extending */
    uint32_t    off;      /* offset inside the 8-byte test block */
} access_t;

static const access_t accesses[] = {
    { "lb",  ld_lb,  ld_lb_p,  1, true,  0 },
    { "lb",  ld_lb,  ld_lb_p,  1, true,  1 },
    { "lb",  ld_lb,  ld_lb_p,  1, true,  2 },
    { "lb",  ld_lb,  ld_lb_p,  1, true,  3 },
    { "lb",  ld_lb,  ld_lb_p,  1, true,  7 },
    { "lbu", ld_lbu, ld_lbu_p, 1, false, 0 },
    { "lh",  ld_lh,  ld_lh_p,  2, true,  0 },
    { "lh",  ld_lh,  ld_lh_p,  2, true,  2 },
    { "lh",  ld_lh,  ld_lh_p,  2, true,  6 },
    { "lhu", ld_lhu, ld_lhu_p, 2, false, 0 },
    { "lw",  ld_lw,  ld_lw_p,  4, true,  0 },
    { "lw",  ld_lw,  ld_lw_p,  4, true,  4 },
    { "lwu", ld_lwu, ld_lwu_p, 4, false, 0 },
    { "ld",  ld_ld,  ld_ld_p,  8, true,  0 },
};
#define N_ACC (int)(sizeof(accesses) / sizeof(accesses[0]))

/* expected GPR value of a load from big-endian bytes b[] */
static uint64_t expect_from_bytes(const uint8_t *b, const access_t *ac)
{
    uint64_t v = 0;
    for (int i = 0; i < ac->size; i++) v = (v << 8) | b[ac->off + i];
    int bits = ac->size * 8;
    if (ac->sign && bits < 64 && (v >> (bits - 1)) & 1)
        v |= ~0ull << bits;
    if (!ac->sign && bits == 32) v &= 0xFFFFFFFFull;
    /* lw is sign-extended from 32 bits */
    return v;
}

/* ------------------------------------------------------------------------ */
/* buffers in installed RAM                                                  */
/* ------------------------------------------------------------------------ */

/* control block with a known non-zero pattern (checks the harness itself) */
static uint64_t ctrl_buf[2] __attribute__((aligned(16))) = {
    0x8192A3B4C5D6E7F8ull, 0
};
/* priming pattern loaded right before the unmapped access */
static uint64_t prime_buf[2] __attribute__((aligned(16))) = {
    0xDEADBEEFCAFEBABEull, 0
};

static int total_checks = 0, total_bad = 0, total_bus = 0, ctrl_bad = 0;

static const char *verdict(uint64_t v, uint64_t exp, bool primed)
{
    if (v == exp) return "OK";
    if (primed) {
        /* does the value look like (part of) the priming pattern? */
        uint64_t p = prime_buf[0];
        for (int sh = 0; sh < 64; sh += 8) {
            uint64_t m = 0xFFull << sh;
            if ((v & m) && (v & m) == (p & m)) return "NG(bus?)";
        }
    }
    return "NG";
}

static void print_val(const char *tag, const access_t *ac, uint32_t addr,
                      uint64_t v, uint64_t exp, bool primed, bool count)
{
    const char *vd = verdict(v, exp, primed);
    if (count) {
        total_checks++;
        if (v != exp) { total_bad++; if (vd[2] == '(') total_bus++; }
    } else if (v != exp) {
        ctrl_bad++;                     /* harness self-check failed */
    }
    outf("%-3s %-4s %08lX = %08lX%08lX %s\n", tag, ac->name,
         (unsigned long)(addr + ac->off),
         (unsigned long)(v >> 32), (unsigned long)(v & 0xFFFFFFFFu), vd);
}

/* all access widths at one 8-byte aligned block (uncached, KSEG1) */
static void test_block(uint32_t phys, bool unmapped, const uint8_t *ctrl_bytes)
{
    uint32_t a = KSEG1(phys);
    uint32_t pa = (uint32_t)UncachedAddr(prime_buf);

    outf("\n-- %s 0x%08lX (uncached KSEG1) --\n",
         unmapped ? "unmapped" : "control ", (unsigned long)phys);

    for (int i = 0; i < N_ACC; i++) {
        const access_t *ac = &accesses[i];
        uint64_t exp = unmapped ? 0 : expect_from_bytes(ctrl_bytes, ac);
        disable_interrupts();
        uint64_t v  = ac->f(a + ac->off);
        uint64_t vp = ac->fp(a + ac->off, pa);
        enable_interrupts();
        print_val("U",  ac, a, v,  exp, false, unmapped);
        print_val("U+P", ac, a, vp, exp, true,  unmapped);
    }
}

/* cached access: D-cache line (16 B) fill from the block */
static void test_cached(uint32_t phys)
{
    uint32_t c = KSEG0(phys & ~15u);
    outf("\n-- unmapped 0x%08lX (cached KSEG0, line fill) --\n", (unsigned long)(phys & ~15u));

    disable_interrupts();
    data_cache_hit_invalidate((void *)c, 16);
    uint64_t w0 = ld_lw(c + 0), w1 = ld_lw(c + 4), w2 = ld_lw(c + 8), w3 = ld_lw(c + 12);
    data_cache_hit_invalidate((void *)c, 16);
    uint64_t d0 = ld_ld(c + 0), d1 = ld_ld(c + 8);
    data_cache_hit_invalidate((void *)c, 16);
    uint64_t b0 = ld_lb(c + 5), h0 = ld_lh(c + 10);
    data_cache_hit_invalidate((void *)c, 16);
    enable_interrupts();

    static const access_t lw0 = { "lw", 0, 0, 4, true, 0 }, ld0 = { "ld", 0, 0, 8, true, 0 };
    static const access_t lb5 = { "lb", 0, 0, 1, true, 5 }, lh10 = { "lh", 0, 0, 2, true, 10 };
    access_t t = lw0;
    t.off = 0;  print_val("C", &t, c, w0, 0, false, true);
    t.off = 4;  print_val("C", &t, c, w1, 0, false, true);
    t.off = 8;  print_val("C", &t, c, w2, 0, false, true);
    t.off = 12; print_val("C", &t, c, w3, 0, false, true);
    t = ld0;
    t.off = 0;  print_val("C", &t, c, d0, 0, false, true);
    t.off = 8;  print_val("C", &t, c, d1, 0, false, true);
    print_val("C", &lb5,  c, b0, 0, false, true);
    print_val("C", &lh10, c, h0, 0, false, true);
}

/* RSP DMA RDRAM -> DMEM (SP DMA address is 24 bits) */
static void sp_dma_rd(uint32_t phys, uint32_t len)
{
    disable_interrupts();
    while (*SP_STATUS & (SP_STATUS_DMA_BUSY | SP_STATUS_DMA_FULL)) {}
    *SP_DMA_SPADDR  = 0;
    *SP_DMA_RAMADDR = phys;
    *SP_DMA_RDLEN   = len - 1;
    while (*SP_STATUS & SP_STATUS_DMA_BUSY) {}
    enable_interrupts();
}

static void test_spdma(uint32_t phys)
{
    volatile uint32_t *dmem = (volatile uint32_t *)0xA4000000u;
    if (phys >= 0x01000000u) {
        outf("\n-- RSP DMA 0x%08lX: skipped (SP DMA address is 24-bit) --\n", (unsigned long)phys);
        return;
    }
    /* keep the 64-byte transfer below the 16 MiB wrap point */
    uint32_t src = phys & ~7u;
    if (src > 0x00FFFFC0u) src = 0x00FFFFC0u;

    for (int i = 0; i < 16; i++) dmem[i] = 0xCCCCCCCCu;
    sp_dma_rd(src, 64);

    uint32_t nz = 0;
    for (int i = 0; i < 16; i++) if (dmem[i] != 0) nz++;
    total_checks++;
    if (nz) total_bad++;
    outf("\n-- RSP DMA RDRAM->DMEM 64 B from 0x%08lX (DMEM pre-filled CC) --\n",
         (unsigned long)src);
    outf("D   %08lX %08lX %08lX %08lX  ... %s (%lu/16 non-zero words)\n",
         (unsigned long)dmem[0], (unsigned long)dmem[1], (unsigned long)dmem[2],
         (unsigned long)dmem[3], nz ? "NG" : "OK", (unsigned long)nz);
}

/* SP DMA across the 24-bit end: 16 B from 0x00FFFFF8.  Expect 8 bytes of 0
 * (unmapped 0x00FFFFF8-F) followed by the first 8 bytes of RDRAM if the
 * address wraps to 0x000000.  Informational, not counted as NG. */
static void test_spdma_wrap(void)
{
    volatile uint32_t *dmem = (volatile uint32_t *)0xA4000000u;
    volatile uint32_t *ram0 = (volatile uint32_t *)0xA0000000u;
    for (int i = 0; i < 4; i++) dmem[i] = 0xCCCCCCCCu;
    sp_dma_rd(0x00FFFFF8u, 16);
    uint32_t r0 = ram0[0], r1 = ram0[1];
    bool wrap = dmem[2] == r0 && dmem[3] == r1;
    outf("\n[SP DMA 24-bit wrap] 16 B from 0x00FFFFF8\n");
    outf("  DMEM %08lX %08lX | %08lX %08lX   RDRAM[0..7] = %08lX %08lX\n",
         (unsigned long)dmem[0], (unsigned long)dmem[1], (unsigned long)dmem[2],
         (unsigned long)dmem[3], (unsigned long)r0, (unsigned long)r1);
    outf("  => first 8 B %s, then %s\n",
         (dmem[0] | dmem[1]) ? "NON-ZERO" : "0 (unmapped)",
         wrap ? "wraps to 0x00000000" : "does NOT match 0x00000000");
}

/* ------------------------------------------------------------------------ */
/* sweep: lw + ld every 256 B over the whole unmapped range, several passes, */
/* with the VI running and with the VI blanked (no VI DMA), to see whether   */
/* stray non-zero values are related to the VI framebuffer fetch.            */
/* ------------------------------------------------------------------------ */

#define VI_CTRL_REG    ((volatile uint32_t *)0xA4400000u)
#define VI_ORIGIN_REG  ((volatile uint32_t *)0xA4400004u)
#define SWEEP_STRIDE   256
#define SWEEP_PASSES   8
#define MAX_STRAY      48

typedef struct {
    uint32_t val;        /* 32-bit value seen (lw, or one half of ld) */
    uint32_t count;
    uint32_t first;      /* first address it was seen at */
    char     kind;       /* 'w' lw, 'H' ld upper, 'L' ld lower */
} stray_t;

static stray_t strays[2][MAX_STRAY];
static int     n_strays[2];
static uint32_t sweep_reads[2], sweep_nz[2], sweep_lost[2];

static void stray_add(int cond, uint32_t val, uint32_t addr, char kind)
{
    for (int i = 0; i < n_strays[cond]; i++)
        if (strays[cond][i].val == val && strays[cond][i].kind == kind) {
            strays[cond][i].count++;
            return;
        }
    if (n_strays[cond] < MAX_STRAY) {
        stray_t *s = &strays[cond][n_strays[cond]++];
        s->val = val; s->count = 1; s->first = addr; s->kind = kind;
    } else {
        sweep_lost[cond]++;
    }
}

/* cond 0: VI running, cond 1: VI blanked.  Interrupts are disabled for the
 * whole condition so nothing (incl. the VI handler) touches VI registers. */
static void sweep_cond(uint32_t start, int cond)
{
    uint32_t vi_ctrl = *VI_CTRL_REG;
    n_strays[cond] = 0;
    sweep_reads[cond] = sweep_nz[cond] = sweep_lost[cond] = 0;

    progress("sweep %s (%d passes, ~2 s)...\n", cond ? "VI blanked" : "VI running", SWEEP_PASSES);
    disable_interrupts();
    if (cond) *VI_CTRL_REG = vi_ctrl & ~3u;          /* type 0 = blank, no VI DMA */

    for (int pass = 0; pass < SWEEP_PASSES; pass++) {
        for (uint32_t p = start; p < RDRAM_SPACE_END; p += SWEEP_STRIDE) {
            uint64_t w = ld_lw(KSEG1(p));
            uint64_t d = ld_ld(KSEG1(p + 8));
            sweep_reads[cond] += 2;
            if (w) { sweep_nz[cond]++; stray_add(cond, (uint32_t)w, p, 'w'); }
            if (d) {
                sweep_nz[cond]++;
                if (d >> 32)          stray_add(cond, (uint32_t)(d >> 32), p + 8, 'H');
                if (d & 0xFFFFFFFFu)  stray_add(cond, (uint32_t)d, p + 8, 'L');
            }
        }
    }

    if (cond) *VI_CTRL_REG = vi_ctrl;                /* restore */
    enable_interrupts();
}

static void sweep_report(uint32_t start)
{
    outf("\n[sweep] 0x%08lX-0x03EFFFFF, lw @+0 and ld @+8 every %d B, %d passes, IRQ off\n",
         (unsigned long)start, SWEEP_STRIDE, SWEEP_PASSES);
    for (int c = 0; c < 2; c++) {
        total_checks += sweep_reads[c];
        total_bad    += sweep_nz[c];
        uint64_t ppm = sweep_reads[c] ? (uint64_t)sweep_nz[c] * 1000000ull / sweep_reads[c] : 0;
        outf("  %s: %lu reads, %lu non-zero (%lu ppm), %d distinct values%s\n",
             c ? "VI blanked" : "VI running",
             (unsigned long)sweep_reads[c], (unsigned long)sweep_nz[c], (unsigned long)ppm,
             n_strays[c], sweep_lost[c] ? " (table full)" : "");
    }
}

/* ------------------------------------------------------------------------ */
/* locate each stray value in installed RDRAM / SP memory                    */
/* ------------------------------------------------------------------------ */

extern char __text_start[], __text_end[], __data_start[], __data_end[];
extern char __bss_start[], __bss_end[];

static const char *region_name(uint32_t phys, uint32_t memsize)
{
    uint32_t fb  = *VI_ORIGIN_REG & 0x00FFFFFFu;
    uint32_t fbs = 640 * 480 * 4;                      /* generous upper bound */
    if (phys >= 0x04000000u && phys < 0x04001000u) return "SP DMEM";
    if (phys >= 0x04001000u && phys < 0x04002000u) return "SP IMEM";
    if (phys < 0x400) return "low vectors";
    if (fb && phys >= fb && phys < fb + fbs) return "near VI_ORIGIN (framebuffer)";
    if (phys >= PhysicalAddr(__text_start) && phys < PhysicalAddr(__text_end)) return ".text";
    if (phys >= PhysicalAddr(__data_start) && phys < PhysicalAddr(__data_end)) return ".data/.rodata";
    if (phys >= PhysicalAddr(__bss_start)  && phys < PhysicalAddr(__bss_end))  return ".bss";
    if (phys >= memsize - 0x10000) return "stack";
    if (phys < PhysicalAddr(sbrk(0))) return "heap";
    return "free RAM";
}

static void locate_strays(uint32_t memsize)
{
    /* merge both conditions into one value list */
    uint32_t vals[2 * MAX_STRAY]; int nv = 0;
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < n_strays[c]; i++) {
            uint32_t v = strays[c][i].val; bool dup = false;
            for (int j = 0; j < nv; j++) if (vals[j] == v) dup = true;
            if (!dup && nv < 2 * MAX_STRAY) vals[nv++] = v;
        }

    outf("\n[stray values] kind w=lw H=ld upper L=ld lower; count VI-run/VI-blank\n");
    outf("  (RAM search excludes this program's stray table and the 64 KiB stack)\n");
    if (!nv) { outf("  none\n"); return; }

    for (int j = 0; j < nv; j++) {
        uint32_t v = vals[j], cnt[2] = {0, 0}, first = 0; char kind = '?';
        for (int c = 0; c < 2; c++)
            for (int i = 0; i < n_strays[c]; i++)
                if (strays[c][i].val == v) {
                    cnt[c] += strays[c][i].count;
                    if (!first) { first = strays[c][i].first; kind = strays[c][i].kind; }
                }

        /* search installed RDRAM and SP DMEM/IMEM (word aligned) */
        uint32_t hits = 0, hit_addr[3];
        volatile uint32_t *ram = (volatile uint32_t *)0xA0000000u;
        uint32_t ex0 = PhysicalAddr(strays), ex1 = ex0 + sizeof(strays);   /* our own table */
        uint32_t st0 = memsize - 0x10000;                                  /* stack (holds vals[]) */
        for (uint32_t k = 0; k < memsize / 4; k++) {
            uint32_t a = k * 4;
            if ((a >= ex0 && a < ex1) || a >= st0) continue;
            if (ram[k] == v) { if (hits < 3) hit_addr[hits] = a; hits++; }
        }
        volatile uint32_t *sp = (volatile uint32_t *)0xA4000000u;
        for (uint32_t k = 0; k < 0x2000 / 4; k++)
            if (sp[k] == v) { if (hits < 3) hit_addr[hits] = 0x04000000u + k * 4; hits++; }

        outf("  %08lX %c  %lu/%lu  first @%08lX  found in RAM: %lu\n",
             (unsigned long)v, kind, (unsigned long)cnt[0], (unsigned long)cnt[1],
             (unsigned long)first, (unsigned long)hits);
        for (uint32_t h = 0; h < hits && h < 3; h++)
            outf("      @%08lX %s\n", (unsigned long)hit_addr[h], region_name(hit_addr[h], memsize));
    }
}

/* ------------------------------------------------------------------------ */
/* write test: store to unmapped addresses, read back, check for aliasing    */
/*                                                                           */
/* Target addresses are chosen congruent to WR_OFF modulo 2 MiB (the size    */
/* of one RDRAM device), so any alias at 2/4/8 MiB granularity lands on      */
/* WR_OFF + k*2MiB inside installed RAM, which is checked to be outside the  */
/* program/heap/stack, saved before and restored after.                      */
/* ------------------------------------------------------------------------ */

#define WR_OFF 0x001C0000u

static void write_test(uint32_t memsize)
{
    uint32_t heap_end = PhysicalAddr(sbrk(0));
    uint32_t cand[4], ncand = 0;
    uint32_t save[4][4];

    outf("\n[write test] sw to unmapped, read back + alias check (2 MiB granularity)\n");
    if (heap_end >= WR_OFF) {
        outf("  skipped: heap end 0x%08lX overlaps alias area\n", (unsigned long)heap_end);
        return;
    }
    for (uint32_t p = WR_OFF; p < memsize; p += 2 * MiB) {
        if (p + 16 > memsize - 0x10000) continue;     /* keep clear of the stack */
        cand[ncand++] = p;
    }

    const uint32_t bases[4] = { memsize, 0x01000000u, 0x02000000u, 0x03C00000u };
    uint32_t targets[4], nt = 0;
    for (int b = 0; b < 4; b++) {
        uint32_t t = (bases[b] & ~(2 * MiB - 1)) + WR_OFF;   /* = WR_OFF mod 2 MiB */
        if (t < bases[b]) t += 2 * MiB;
        if (t + 16 <= RDRAM_SPACE_END) targets[nt++] = t;
    }

    for (uint32_t i = 0; i < nt; i++) {
        volatile uint32_t *u = (volatile uint32_t *)KSEG1(targets[i]);
        uint32_t pat[4] = { 0x11223344u ^ targets[i], 0x55667788u, 0x99AABBCCu, 0xDDEEFF00u ^ i };
        uint32_t rb[4], alias_hits = 0;

        disable_interrupts();
        for (uint32_t c = 0; c < ncand; c++)
            for (int k = 0; k < 4; k++) save[c][k] = ((volatile uint32_t *)KSEG1(cand[c]))[k];
        for (int k = 0; k < 4; k++) u[k] = pat[k];
        for (int k = 0; k < 4; k++) rb[k] = u[k];
        for (uint32_t c = 0; c < ncand; c++) {
            volatile uint32_t *m = (volatile uint32_t *)KSEG1(cand[c]);
            for (int k = 0; k < 4; k++) if (m[k] != save[c][k]) alias_hits |= 1u << c;
            for (int k = 0; k < 4; k++) m[k] = save[c][k];
        }
        enable_interrupts();

        uint32_t nz = 0;
        for (int k = 0; k < 4; k++) if (rb[k]) nz++;
        total_checks += 1; if (nz || alias_hits) total_bad++;
        outf("  0x%08lX: wrote %08lX.. read back %08lX %08lX %08lX %08lX %s",
             (unsigned long)targets[i], (unsigned long)pat[0],
             (unsigned long)rb[0], (unsigned long)rb[1], (unsigned long)rb[2], (unsigned long)rb[3],
             nz ? "NG" : "OK");
        if (alias_hits) {
            outf("  ALIAS ->");
            for (uint32_t c = 0; c < ncand; c++)
                if (alias_hits & (1u << c)) outf(" %08lX", (unsigned long)cand[c]);
        }
        outf("\n");
    }
}

/* ------------------------------------------------------------------------ */
/* uncached lw latency: installed vs unmapped                                 */
/* ------------------------------------------------------------------------ */

__attribute__((noinline))
static uint32_t lat_run(uint32_t base)
{
    disable_interrupts();
    uint32_t t0 = TICKS_READ();
    for (uint32_t i = 0; i < 1024; i++) (void)ld_lw(base + i * 64);
    uint32_t dt = TICKS_READ() - t0;
    enable_interrupts();
    return dt;
}

static void latency(uint32_t memsize)
{
    uint32_t tm = lat_run(KSEG1(0x00200000u));
    uint32_t tu = lat_run(KSEG1(memsize));
    outf("\n[latency] 1024 uncached lw, stride 64 B\n");
    outf("  installed 0x00200000: %lu.%lu CPU cycles/read\n",
         (unsigned long)(tm * 2 / 1024), (unsigned long)((tm * 20 / 1024) % 10));
    outf("  unmapped  0x%08lX: %lu.%lu CPU cycles/read\n", (unsigned long)memsize,
         (unsigned long)(tu * 2 / 1024), (unsigned long)((tu * 20 / 1024) % 10));
}

/* ------------------------------------------------------------------------ */
/* save to SD (new numbered file, never overwrite)                           */
/* ------------------------------------------------------------------------ */

static int save_to_sd(void)
{
    char path[80];
    struct stat st;
    int n;

    if (!debug_init_sdfs("sd:/", -1)) { outf("\nNO SD CARD - NOT SAVED\n"); return 0; }
    const char *dir = OUT_DIR;
    if (mkdir(OUT_DIR, 0777) != 0 && errno != EEXIST) dir = "sd:";
    for (n = 1; n < 1000; n++) {
        char name[48];
        snprintf(name, sizeof name, OUT_NAME, n);
        snprintf(path, sizeof path, "%s/%s", dir, name);
        if (stat(path, &st) != 0) break;
    }
    if (n >= 1000) { outf("\nSD: no free file name\n"); debug_close_sdfs(); return 0; }

    disable_interrupts();
    FILE *f = fopen(path, "w");
    int ok = f && fwrite(report, 1, report_len, f) == (size_t)report_len;
    if (f && fclose(f) != 0) ok = 0;
    enable_interrupts();
    debug_close_sdfs();
    outf(ok ? "\nSAVED %s\n" : "\nSD WRITE FAILED %s\n", path);
    return ok;
}

/* ------------------------------------------------------------------------ */

static const char *cart_name(void)
{
    static const char *names[] = { "64drive", "EverDrive-64 X", "EverDrive-64 V1-3/ED64+", "SummerCart64" };
    return (cart_type >= 0 && cart_type < CART_MAX) ? names[cart_type] : "unknown/none";
}

static void run(void)
{
    uint32_t memsize = get_memory_size();
    report_len = 0; report[0] = 0;
    total_checks = total_bad = total_bus = ctrl_bad = 0;
    console_clear();

    /* the ISViewer/USB log gets exactly the lines of the SD file, framed by
       markers so one run can be cut out of a longer emulator log */
    debugf("===== BEGIN rdram_unmapped v" VERSION " report =====\n");
    outf("N64 RDRAM unmapped-read test v" VERSION " (libdragon)\n");
    outf("TV:%s RAM:%u MiB ExpPak:%s Cart:%s\n",
         get_tv_type() == TV_PAL ? "PAL" : get_tv_type() == TV_MPAL ? "MPAL" : "NTSC",
         (unsigned)(memsize / MiB), is_memory_expanded() ? "yes" : "no", cart_name());
    outf("log: SD file + USB:%s ISViewer:%s\n", log_usb ? "yes" : "no", log_isv ? "yes" : "no");
    outf("range 0x%08lX-0x03EFFFFF, expected value 0 for every access\n", (unsigned long)memsize);
    outf("U=uncached  U+P=uncached after ld of DEADBEEFCAFEBABE  C=cached\n");
    outf("value = full 64-bit GPR after the load\n");

    /* 1. control: the harness must read the known pattern correctly */
    data_cache_hit_writeback_invalidate(ctrl_buf, sizeof ctrl_buf);
    data_cache_hit_writeback_invalidate(prime_buf, sizeof prime_buf);
    uint8_t cb[16];
    memcpy(cb, ctrl_buf, 16);
    quiet = true;
    test_block(PhysicalAddr(ctrl_buf), false, cb);
    quiet = false;
    outf("harness self-check on 0x%08lX (8192A3B4C5D6E7F8): %s\n",
         (unsigned long)PhysicalAddr(ctrl_buf), ctrl_bad ? "NG - results invalid" : "OK");

    /* 2. unmapped blocks, all widths */
    uint32_t blocks[8], nb = 0;
    blocks[nb++] = memsize;                       /* first unmapped byte */
    if (memsize < 8 * MiB) blocks[nb++] = 0x00400000u + 0x123450u;
    if (memsize < 8 * MiB) blocks[nb++] = 0x007FFFF8u;
    blocks[nb++] = 0x00800000u == memsize ? 0x00C00000u : 0x00800000u;
    blocks[nb++] = 0x00FFFFF8u;
    blocks[nb++] = 0x01000000u;
    blocks[nb++] = 0x02ABCDE8u;
    blocks[nb++] = 0x03EFFFF8u;                   /* contains 0x03EFFFFF */

    quiet = true;                                 /* detail goes to the file */
    for (uint32_t i = 0; i < nb; i++) {
        progress("block 0x%08lX...\n", (unsigned long)blocks[i]);
        test_block(blocks[i], true, NULL);
    }
    for (uint32_t i = 0; i < nb; i++) {
        progress("cached 0x%08lX...\n", (unsigned long)blocks[i]);
        test_cached(blocks[i]);
    }
    for (uint32_t i = 0; i < nb; i++) test_spdma(blocks[i]);
    test_spdma_wrap();
    quiet = false;

    /* 3. sweep with VI running / blanked, 4. locate strays,
       5. write/alias, 6. latency */
    sweep_cond(memsize, 0);
    sweep_cond(memsize, 1);
    sweep_report(memsize);
    locate_strays(memsize);
    write_test(memsize);
    latency(memsize);

    outf("\n[summary] %d checks, %d non-zero/NG", total_checks, total_bad);
    if (total_bus) outf(" (%d look like the primed bus value)", total_bus);
    outf("\n=> %s\n", total_bad ? "NON-ZERO DATA FOUND (see file)" : "ALL UNMAPPED READS RETURNED 0");

    save_to_sd();
    debugf("===== END rdram_unmapped v" VERSION " report =====\n");
    outf("A: run again\n");
}

int main(void)
{
    console_init();
    console_set_render_mode(RENDER_MANUAL);
    /* libdragon's console mirrors every printf to stderr (= USB/ISViewer log)
       by default.  outf() already sends each line to the log with debugf(),
       so turn the mirror off; otherwise every on-screen line appears twice. */
    console_set_debug(false);
    log_usb = debug_init_usblog();
    log_isv = debug_init_emulog();      /* ISViewer: emulators without SD */
    joypad_init();
    rsp_init();                         /* halt RSP: we own SP DMA and DMEM */
    if (!sys_bbplayer()) cart_init();   /* for the cart name in the report */

    run();
    while (1) {
        joypad_poll();
        if (joypad_get_buttons_pressed(JOYPAD_PORT_1).a) run();
        console_render();
    }
}
