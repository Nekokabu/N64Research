/*
 * n64_memspeed - RDRAM access speed test ROM for the Nintendo 64
 *
 * Built with libdragon.  Two modes, chosen from a start menu:
 *   A - RDRAM speed test: CPU (VR4300) and RSP-DMA access speed, separately
 *       for the base 4 MiB and for the Expansion Pak 4 MiB (4-8 MiB).
 *   B - RDRAM read/write test: every byte of installed RDRAM is tested with
 *       six patterns by a kernel running from RSP DMEM (memtest_kernel.S).
 * Reports are plain text, saved to the SD card as new numbered files
 * (sd:/n64_memspeed/n64_memspeed_v1_NNN.txt / n64_ramtest_v1_NNN.txt, or the
 * SD root if the directory cannot be created).  Existing files are never
 * overwritten.  The same text is printed on screen and to the USB debug log.
 *
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
#include <libcart/cart.h>   /* cart_type: flashcart detected by libcart */

#define VERSION          "1.3"
#define OUT_DIR          "sd:/n64_memspeed"            /* used if mkdir works */
/* file names: n64_memspeed_v1_NNN.txt / n64_ramtest_v1_NNN.txt, never overwritten */

#define MiB              (1024u * 1024u)
#define WIN_SIZE         MiB              /* size of each test window      */
#define WIN_BASE_PHYS    0x00200000u      /* base RDRAM window:   2-3 MiB  */
#define WIN_EXP_PHYS     0x00600000u      /* expansion window:    6-7 MiB  */
#define RUNS             3                /* best-of-N for every test      */
#define CHASE_STRIDE     64               /* bytes between pointer slots   */
#define CHASE_HOPS       (2 * (WIN_SIZE / CHASE_STRIDE))
#define SPDMA_CHUNK      4096             /* one RSP DMA transfer (DMEM)   */

#define KSEG0(phys)      ((void *)(0x80000000u | (phys)))
#define KSEG1(phys)      ((void *)(0xA0000000u | (phys)))

/* ------------------------------------------------------------------------ */
/* Report buffer: everything goes to screen, USB log and the SD text file.   */
/* ------------------------------------------------------------------------ */

static char report[32 * 1024];
static int  report_len = 0;

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
    printf("%s", line);
    debugf("%s", line);
    console_render();
}

/* ------------------------------------------------------------------------ */
/* Timing helpers                                                            */
/* ------------------------------------------------------------------------ */

typedef uint64_t (*test_fn)(void *base, uint32_t size);

/* Run fn RUNS times with interrupts disabled and return the fastest time
 * in timer ticks (TICKS_PER_SECOND = CPU clock / 2). */
static uint32_t time_test(test_fn fn, void *base, uint32_t size,
                          bool cached_prep, bool cached_flush)
{
    uint32_t best = 0xFFFFFFFFu;
    for (int r = 0; r < RUNS; r++) {
        if (cached_prep)
            data_cache_hit_writeback_invalidate(base, size);
        disable_interrupts();
        uint32_t t0 = TICKS_READ();
        fn(base, size);
        uint32_t dt = TICKS_READ() - t0;
        enable_interrupts();
        if (cached_flush)
            data_cache_hit_writeback_invalidate(base, size);
        if (dt < best) best = dt;
    }
    return best;
}

/* ticks -> nanoseconds (64-bit intermediate) */
static uint64_t ticks_to_ns(uint64_t ticks)
{
    return ticks * 1000000000ull / TICKS_PER_SECOND;
}

/* Print one result line: throughput in MB/s plus per-operation cost. */
static void report_rate(const char *name, uint32_t ticks, uint32_t bytes, uint32_t ops)
{
    uint64_t ns      = ticks_to_ns(ticks);
    /* MB/s * 100, MB = 10^6 bytes */
    uint64_t mbps100 = (ns ? ((uint64_t)bytes * 100000ull) / ns : 0);
    uint64_t nsop100 = ns * 100 / ops;
    uint64_t cyc10   = (uint64_t)ticks * 2 * 10 / ops;   /* CPU cycles */
    outf("%-22s %4u.%02u MB/s %5u.%02u ns/op %4u.%u cyc/op\n",
         name,
         (unsigned)(mbps100 / 100), (unsigned)(mbps100 % 100),
         (unsigned)(nsop100 / 100), (unsigned)(nsop100 % 100),
         (unsigned)(cyc10 / 10),   (unsigned)(cyc10 % 10));
}

static void report_latency(const char *name, uint32_t ticks, uint32_t hops)
{
    uint64_t ns      = ticks_to_ns(ticks);
    uint64_t nshop100 = ns * 100 / hops;
    uint64_t cyc10    = (uint64_t)ticks * 2 * 10 / hops;
    outf("%-22s %5u.%02u ns/hop %5u.%u cyc/hop\n", name,
         (unsigned)(nshop100 / 100), (unsigned)(nshop100 % 100),
         (unsigned)(cyc10 / 10),   (unsigned)(cyc10 % 10));
}

/* ------------------------------------------------------------------------ */
/* CPU sequential access kernels (8x unrolled, volatile so nothing is        */
/* optimised away).  "base" is a KSEG0 (cached) or KSEG1 (uncached) address. */
/* ------------------------------------------------------------------------ */

__attribute__((noinline))
static uint64_t k_read64(void *base, uint32_t size)
{
    volatile uint64_t *p = base;
    uint32_t n = size / 8;
    uint64_t a = 0, b = 0, c = 0, d = 0;
    for (uint32_t i = 0; i < n; i += 8) {
        a += p[i+0]; b += p[i+1]; c += p[i+2]; d += p[i+3];
        a += p[i+4]; b += p[i+5]; c += p[i+6]; d += p[i+7];
    }
    return a + b + c + d;
}

__attribute__((noinline))
static uint64_t k_read32(void *base, uint32_t size)
{
    volatile uint32_t *p = base;
    uint32_t n = size / 4;
    uint32_t a = 0, b = 0, c = 0, d = 0;
    for (uint32_t i = 0; i < n; i += 8) {
        a += p[i+0]; b += p[i+1]; c += p[i+2]; d += p[i+3];
        a += p[i+4]; b += p[i+5]; c += p[i+6]; d += p[i+7];
    }
    return a + b + c + d;
}

__attribute__((noinline))
static uint64_t k_write64(void *base, uint32_t size)
{
    volatile uint64_t *p = base;
    uint32_t n = size / 8;
    uint64_t v = 0x0123456789ABCDEFull;
    for (uint32_t i = 0; i < n; i += 8) {
        p[i+0] = v; p[i+1] = v; p[i+2] = v; p[i+3] = v;
        p[i+4] = v; p[i+5] = v; p[i+6] = v; p[i+7] = v;
    }
    return 0;
}

__attribute__((noinline))
static uint64_t k_write32(void *base, uint32_t size)
{
    volatile uint32_t *p = base;
    uint32_t n = size / 4;
    uint32_t v = 0x89ABCDEFu;
    for (uint32_t i = 0; i < n; i += 8) {
        p[i+0] = v; p[i+1] = v; p[i+2] = v; p[i+3] = v;
        p[i+4] = v; p[i+5] = v; p[i+6] = v; p[i+7] = v;
    }
    return 0;
}

/* memcpy lower half -> upper half of the window (cached) */
__attribute__((noinline))
static uint64_t k_memcpy(void *base, uint32_t size)
{
    uint8_t *p = base;
    memcpy(p + size / 2, p, size / 2);
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Random-access latency: pointer chasing over a random cyclic permutation   */
/* ------------------------------------------------------------------------ */

static uint32_t rng_state = 0x2545F491u;
static uint32_t rng(void)
{
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

/* Build a single-cycle random permutation (Sattolo) in the window.  Each
 * slot (CHASE_STRIDE bytes apart) holds the *slot index* of the next hop.
 * Written through the uncached segment so no dirty lines are left behind. */
static void chase_build(uint32_t phys, uint32_t size)
{
    volatile uint32_t *u = KSEG1(phys);
    uint32_t slots = size / CHASE_STRIDE;
    uint32_t words = CHASE_STRIDE / 4;

    /* identity */
    for (uint32_t i = 0; i < slots; i++) u[i * words] = i;
    /* Sattolo shuffle -> one Hamiltonian cycle */
    for (uint32_t i = slots - 1; i > 0; i--) {
        uint32_t j = rng() % i;
        uint32_t t = u[i * words];
        u[i * words] = u[j * words];
        u[j * words] = t;
    }
}

__attribute__((noinline))
static uint64_t k_chase(void *base, uint32_t size)
{
    (void)size;
    volatile uint32_t *p = base;
    uint32_t idx = 0;
    const uint32_t words = CHASE_STRIDE / 4;
    for (uint32_t h = 0; h < CHASE_HOPS; h += 4) {
        idx = p[idx * words];
        idx = p[idx * words];
        idx = p[idx * words];
        idx = p[idx * words];
    }
    return idx;
}

/* ------------------------------------------------------------------------ */
/* RSP DMA bandwidth (RDRAM <-> DMEM), raw SP registers                      */
/* ------------------------------------------------------------------------ */

static inline void sp_dma_wait_slot(void)
{
    while (*SP_STATUS & (SP_STATUS_DMA_BUSY | SP_STATUS_DMA_FULL)) {}
}

static inline void sp_dma_wait_idle(void)
{
    while (*SP_STATUS & SP_STATUS_DMA_BUSY) {}
}

/* RDRAM -> DMEM */
__attribute__((noinline))
static uint64_t k_spdma_read(void *base, uint32_t size)
{
    uint32_t phys = PhysicalAddr(base);
    for (uint32_t off = 0; off < size; off += SPDMA_CHUNK) {
        sp_dma_wait_slot();
        *SP_DMA_SPADDR  = 0;                  /* DMEM offset 0 */
        *SP_DMA_RAMADDR = phys + off;
        *SP_DMA_RDLEN   = SPDMA_CHUNK - 1;
    }
    sp_dma_wait_idle();
    return 0;
}

/* DMEM -> RDRAM */
__attribute__((noinline))
static uint64_t k_spdma_write(void *base, uint32_t size)
{
    uint32_t phys = PhysicalAddr(base);
    for (uint32_t off = 0; off < size; off += SPDMA_CHUNK) {
        sp_dma_wait_slot();
        *SP_DMA_SPADDR  = 0;
        *SP_DMA_RAMADDR = phys + off;
        *SP_DMA_WRLEN   = SPDMA_CHUNK - 1;
    }
    sp_dma_wait_idle();
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Pattern integrity check (uncached write, then uncached read-back)         */
/* ------------------------------------------------------------------------ */

static uint32_t pattern_check(uint32_t phys, uint32_t size)
{
    volatile uint32_t *u = KSEG1(phys);
    uint32_t n = size / 4, errors = 0;

    for (uint32_t i = 0; i < n; i++) u[i] = (phys + i * 4) ^ 0xA5A5A5A5u;
    for (uint32_t i = 0; i < n; i++)
        if (u[i] != ((phys + i * 4) ^ 0xA5A5A5A5u)) errors++;

    for (uint32_t i = 0; i < n; i++) u[i] = ~(phys + i * 4);
    for (uint32_t i = 0; i < n; i++)
        if (u[i] != ~(phys + i * 4)) errors++;

    return errors;
}

/* ------------------------------------------------------------------------ */
/* One full test suite on a 1 MiB window                                     */
/* ------------------------------------------------------------------------ */

static void run_window(const char *label, uint32_t phys)
{
    void *c = KSEG0(phys);   /* cached   */
    void *u = KSEG1(phys);   /* uncached */
    uint32_t t;

    outf("\n[%s] 0x%08lX-0x%08lX (%u KiB window)\n",
         label, (unsigned long)(0x80000000u | phys),
         (unsigned long)((0x80000000u | phys) + WIN_SIZE - 1), WIN_SIZE / 1024);

    uint32_t err = pattern_check(phys, WIN_SIZE);
    outf("pattern check          %s (%lu errors)\n", err ? "FAIL" : "OK", (unsigned long)err);

    /* --- CPU cached (KSEG0) --- */
    t = time_test(k_read64,  c, WIN_SIZE, true,  false);
    report_rate("cached   read  ld seq", t, WIN_SIZE, WIN_SIZE / 8);
    t = time_test(k_read32,  c, WIN_SIZE, true,  false);
    report_rate("cached   read  lw seq", t, WIN_SIZE, WIN_SIZE / 4);
    t = time_test(k_write64, c, WIN_SIZE, true,  true);
    report_rate("cached   write sd seq", t, WIN_SIZE, WIN_SIZE / 8);
    t = time_test(k_write32, c, WIN_SIZE, true,  true);
    report_rate("cached   write sw seq", t, WIN_SIZE, WIN_SIZE / 4);
    t = time_test(k_memcpy,  c, WIN_SIZE, true,  true);
    report_rate("cached   memcpy 512K ", t, WIN_SIZE / 2, WIN_SIZE / 2 / 8);

    /* --- CPU uncached (KSEG1) --- */
    t = time_test(k_read64,  u, WIN_SIZE, false, false);
    report_rate("uncached read  ld seq", t, WIN_SIZE, WIN_SIZE / 8);
    t = time_test(k_read32,  u, WIN_SIZE, false, false);
    report_rate("uncached read  lw seq", t, WIN_SIZE, WIN_SIZE / 4);
    t = time_test(k_write64, u, WIN_SIZE, false, false);
    report_rate("uncached write sd seq", t, WIN_SIZE, WIN_SIZE / 8);
    t = time_test(k_write32, u, WIN_SIZE, false, false);
    report_rate("uncached write sw seq", t, WIN_SIZE, WIN_SIZE / 4);

    /* --- Random access latency --- */
    chase_build(phys, WIN_SIZE);
    t = time_test(k_chase, c, WIN_SIZE, true, false);
    report_latency("random cached  (miss)", t, CHASE_HOPS);
    t = time_test(k_chase, u, WIN_SIZE, false, false);
    report_latency("random uncached lw   ", t, CHASE_HOPS);

    /* --- RSP DMA --- */
    data_cache_hit_writeback_invalidate(c, WIN_SIZE);
    t = time_test(k_spdma_read,  u, WIN_SIZE, false, false);
    report_rate("RSP DMA RDRAM->DMEM  ", t, WIN_SIZE, WIN_SIZE / SPDMA_CHUNK);
    t = time_test(k_spdma_write, u, WIN_SIZE, false, false);
    report_rate("RSP DMA DMEM->RDRAM  ", t, WIN_SIZE, WIN_SIZE / SPDMA_CHUNK);
}

/* Read-only sweep over every MiB present: cached + uncached 64-bit reads.
 * Nothing is written, so it is safe to cover code, heap and stack too. */
static void run_sweep(uint32_t memsize)
{
    outf("\n[MiB sweep] read-only, ld seq, MB/s  (cached / uncached)\n");
    for (uint32_t mb = 0; mb < memsize / MiB; mb++) {
        uint32_t phys = mb * MiB;
        uint32_t tc = time_test(k_read64, KSEG0(phys), MiB, true,  false);
        uint32_t tu = time_test(k_read64, KSEG1(phys), MiB, false, false);
        uint64_t nc = ticks_to_ns(tc), nu = ticks_to_ns(tu);
        uint64_t mc = (uint64_t)MiB * 100000ull / nc;
        uint64_t mu = (uint64_t)MiB * 100000ull / nu;
        outf("MiB %u  0x%08lX  %4u.%02u / %4u.%02u%s\n", (unsigned)mb,
             (unsigned long)(0x80000000u | phys),
             (unsigned)(mc / 100), (unsigned)(mc % 100),
             (unsigned)(mu / 100), (unsigned)(mu % 100),
             mb >= 4 ? "  (Expansion Pak)" : "");
    }
}

/* ------------------------------------------------------------------------ */

static bool window_is_safe(uint32_t phys, uint32_t memsize, const char **why)
{
    uint32_t start = 0x80000000u | phys;
    uint32_t end   = start + WIN_SIZE;
    uint32_t heap_end = (uint32_t)sbrk(0);                     /* current heap top */
    uint32_t stack_lo = 0x80000000u + memsize - 0x10000;        /* 64 KiB stack  */

    if (phys + WIN_SIZE > memsize) { *why = "beyond installed RAM"; return false; }
    if (start < heap_end)          { *why = "overlaps program/heap"; return false; }
    if (end > stack_lo)            { *why = "overlaps stack";        return false; }
    return true;
}

/* Save the report to the SD card under a new, numbered file name.  An
 * existing file is never overwritten: the first free number is used.
 * namefmt must contain one %03d. */
static int save_to_sd(const char *namefmt)
{
    char path[80];
    struct stat st;
    int n;

    if (!debug_init_sdfs("sd:/", -1)) {
        outf("\nNO SD CARD - NOT SAVED\n");
        return 0;
    }

    /* Prefer a sub-directory; fall back to the root if the FAT layer of this
     * libdragon build has no mkdir (ENOSYS) or the directory can't be made. */
    const char *dir = OUT_DIR;
    if (mkdir(OUT_DIR, 0777) != 0 && errno != EEXIST)
        dir = "sd:";

    for (n = 1; n < 1000; n++) {
        char name[48];
        snprintf(name, sizeof name, namefmt, n);
        snprintf(path, sizeof path, "%s/%s", dir, name);
        if (stat(path, &st) != 0) break;     /* not there -> free name */
    }
    if (n >= 1000) {
        outf("\nSD: no free file name in %s/\n", dir);
        debug_close_sdfs();
        return 0;
    }

    disable_interrupts();
    FILE *f = fopen(path, "w");
    int ok = f && fwrite(report, 1, report_len, f) == (size_t)report_len;
    if (f && fclose(f) != 0) ok = 0;
    enable_interrupts();
    debug_close_sdfs();

    outf(ok ? "\nSAVED %s\n" : "\nSD WRITE FAILED %s\n", path);
    return ok;
}

static const char *cart_name(void)
{
    static const char *names[] = { "64drive", "EverDrive-64 X", "EverDrive-64 V1-3/ED64+", "SummerCart64" };
    return (cart_type >= 0 && cart_type < CART_MAX) ? names[cart_type] : "unknown/none";
}

static const char *tv_name(void)
{
    return get_tv_type() == TV_PAL ? "PAL" : get_tv_type() == TV_MPAL ? "MPAL" : "NTSC";
}

static void report_header(const char *title)
{
    uint32_t memsize = get_memory_size();
    report_len = 0; report[0] = 0;
    console_clear();
    outf("%s v" VERSION " (libdragon)\n", title);
    outf("TV:%s  RAM:%u MiB  ExpPak:%s  CPU:%u.%02u MHz  Timer:%u.%03u MHz\n",
         tv_name(), (unsigned)(memsize / MiB), is_memory_expanded() ? "yes" : "no",
         (unsigned)(CPU_FREQUENCY / 1000000), (unsigned)((CPU_FREQUENCY / 10000) % 100),
         (unsigned)(TICKS_PER_SECOND / 1000000), (unsigned)((TICKS_PER_SECOND / 1000) % 1000));
    outf("Cart: %s\n", cart_name());
}

/* ------------------------------------------------------------------------ */
/* Mode A: RDRAM speed test                                                  */
/* ------------------------------------------------------------------------ */

static void run_speed_test(void)
{
    uint32_t memsize = get_memory_size();
    const char *why = "";

    report_header("N64 RDRAM speed test");
    outf("best of %d runs, IRQ off, MB=10^6 B, op=ld/sd 8B lw/sw 4B DMA 4KiB\n", RUNS);

    if (window_is_safe(WIN_BASE_PHYS, memsize, &why))
        run_window("Region A: base RDRAM (0-4 MiB)", WIN_BASE_PHYS);
    else
        outf("\n[Region A] skipped: %s\n", why);

    if (is_memory_expanded() && window_is_safe(WIN_EXP_PHYS, memsize, &why))
        run_window("Region B: Expansion Pak (4-8 MiB)", WIN_EXP_PHYS);
    else if (!is_memory_expanded())
        outf("\n[Region B] skipped: no Expansion Pak (4 MiB console)\n");
    else
        outf("\n[Region B] skipped: %s\n", why);

    run_sweep(memsize);

    save_to_sd("n64_memspeed_v1_%03d.txt");
}

/* ------------------------------------------------------------------------ */
/* Mode B: full RDRAM read/write test                                        */
/*                                                                           */
/* The test kernel (memtest_kernel.S) is copied into RSP DMEM and executed   */
/* from there, so every byte of RDRAM - including the memory this program,   */
/* its heap, stack and the framebuffers live in - is tested.  Each 4 KiB     */
/* chunk is saved to RSP IMEM, written/verified with six patterns, and       */
/* restored, so the program survives.  Interrupts are off for the whole run  */
/* and the screen shows noise while the framebuffers are being tested.       */
/* ------------------------------------------------------------------------ */

extern const uint32_t __memtest_kernel_start[];
extern const uint32_t __memtest_kernel_end[];

#define SP_DMEM_BASE   0xA4000000u
#define SP_IMEM_BASE   0xA4001000u
#define RES_BASE       (SP_DMEM_BASE + 0xF00)   /* result block in DMEM */
#define RES_WORDS      16
typedef void (*memtest_fn)(uint32_t start, uint32_t size, uint32_t buf, uint32_t res);

static void run_ram_test(void)
{
    uint32_t memsize = get_memory_size();
    volatile uint32_t *dmem = (volatile uint32_t *)SP_DMEM_BASE;
    volatile uint32_t *res  = (volatile uint32_t *)RES_BASE;
    uint32_t klen = (uint32_t)((const uint8_t *)__memtest_kernel_end - (const uint8_t *)__memtest_kernel_start);

    report_header("N64 RDRAM read/write test");
    outf("range 0x80000000-0x%08lX (%u MiB), 4 KiB chunks, save/test/restore\n",
         (unsigned long)(0x80000000u + memsize - 1), (unsigned)(memsize / MiB));
    outf("patterns: 00000000 FFFFFFFF AAAAAAAA 55555555 ADDR ~ADDR\n");
    outf("kernel runs from SP DMEM (%lu bytes) -> every byte is tested.\n", (unsigned long)klen);
    outf("Screen shows noise while running (IRQ off, ~10-30 s)...\n");

    if (klen > 0xF00) { outf("ERROR: kernel too large for DMEM\n"); return; }

    /* copy kernel into DMEM with 32-bit stores, clear the result block */
    for (uint32_t i = 0; i < klen / 4; i++) dmem[i] = __memtest_kernel_start[i];
    for (uint32_t i = 0; i < RES_WORDS; i++) res[i] = 0;

    disable_interrupts();
    /* make RAM consistent with the caches: everything the kernel saves and
     * restores must be the real data, and no stale lines may survive */
    data_cache_index_writeback_invalidate(KSEG0_START_ADDR, 8 * 1024);
    inst_cache_index_invalidate(KSEG0_START_ADDR, 16 * 1024);

    uint32_t t0 = TICKS_READ();
    ((memtest_fn)SP_DMEM_BASE)(0xA0000000u, memsize, SP_IMEM_BASE, RES_BASE);
    uint32_t dt = TICKS_READ() - t0;

    data_cache_index_writeback_invalidate(KSEG0_START_ADDR, 8 * 1024);
    inst_cache_index_invalidate(KSEG0_START_ADDR, 16 * 1024);
    enable_interrupts();

    uint32_t errors = res[0];
    uint64_t ms = ticks_to_ns(dt) / 1000000ull;

    outf("\nresult: %s   errors: %lu   time: %lu.%lu s\n",
         errors ? "FAIL" : "PASS", (unsigned long)errors,
         (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 100));
    if (errors)
        outf("first error: addr 0x%08lX expected %08lX read %08lX\n",
             (unsigned long)(res[1] & 0x1FFFFFFFu) | 0x80000000ul,
             (unsigned long)res[2], (unsigned long)res[3]);
    outf("errors per MiB:");
    for (uint32_t mb = 0; mb < memsize / MiB; mb++)
        outf(" %lu", (unsigned long)res[8 + mb]);
    outf("\n");

    /* on a 4 MiB console, peek at the (absent) Expansion Pak area */
    if (memsize < 8 * MiB) {
        volatile uint32_t *x = (volatile uint32_t *)0xA0400000u;
        uint32_t before = x[0];
        x[0] = 0x12345678u; uint32_t a = x[0];
        x[0] = 0xEDCBA987u; uint32_t b = x[0];
        x[0] = before;
        outf("4-8 MiB area probe: wrote 12345678/EDCBA987 read %08lX/%08lX -> %s\n",
             (unsigned long)a, (unsigned long)b,
             (a == 0x12345678u && b == 0xEDCBA987u) ? "RESPONDS (?)" : "no memory (as expected)");
    }

    save_to_sd("n64_ramtest_v1_%03d.txt");
}

/* ------------------------------------------------------------------------ */

static void show_menu(void)
{
    console_clear();
    printf("N64 RDRAM test v" VERSION " (libdragon)\n\n");
    printf("TV: %s   RAM: %u MiB   Expansion Pak: %s\n",
           tv_name(), (unsigned)(get_memory_size() / MiB), is_memory_expanded() ? "yes" : "no");
    printf("Cart: %s\n\n", cart_name());
    printf("  A : RDRAM speed test\n");
    printf("      (CPU cached/uncached, latency, RSP DMA, per-MiB sweep)\n\n");
    printf("  B : RDRAM read/write test\n");
    printf("      (all %u MiB, 6 patterns, non-destructive)\n\n", (unsigned)(get_memory_size() / MiB));
    printf("Results are saved to SD as new numbered .txt files.\n");
    console_render();
}

int main(void)
{
    console_init();
    console_set_render_mode(RENDER_MANUAL);
    debug_init_usblog();
    joypad_init();
    rsp_init();                     /* halts the RSP: we own SP DMA, DMEM and IMEM */

    /* Detect the flashcart at boot so cart_type is valid for the menu and
     * reports (SD is only mounted at save time).  Skip on iQue, where
     * touching PI addresses outside ROM causes a bus error.  Calling it
     * again later from debug_init_sdfs() is harmless. */
    if (!sys_bbplayer())
        cart_init();

    show_menu();
    while (1) {
        joypad_poll();
        joypad_buttons_t b = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        if (b.a || b.b) {
            if (b.a) run_speed_test(); else run_ram_test();
            outf("\nA: speed test   B: read/write test   START: menu\n");
        } else if (b.start) {
            show_menu();
        }
        console_render();
    }
}
