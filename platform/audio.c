/* SPDX-License-Identifier: MIT */
/* ALNK0 DMA output: render one block per interrupt and commit it safely. */
#include <string.h>

#include "fm1.h"
#include "fm1_app.h"
#include "fm1_device.h"
#include "fm1_engine_control.h"
#include "fm1_audio_io.h"
#include "board.h"
#include "mixer.h"

#define AUDIO_IRQ_INDEX 11U
#define AUDIO_IRQ_PRIORITY 4U
#define AUDIO_HARDWARE_RATE 44118U

#define ALNK_TX_START (1U << 11)
#define ALNK_HALF_FREE (1U << 15)
#define ALNK_PENDING(half) (1U << (half))
#define ALNK_PENDING_MASK 15U

#define DMA_ALIGNED __attribute__((aligned(32)))

static int32_t capture[2][FM1_BLOCK_WORDS] DMA_ALIGNED;
static int32_t playback[2][FM1_BLOCK_WORDS] DMA_ALIGNED;
static int32_t block[FM1_BLOCK_WORDS] DMA_ALIGNED;
static struct fm1_audio_stats audio_stats;
static uint32_t block_ticks;
static uint32_t last_tick;
static uint8_t running;
static uint8_t have_tick;
static volatile unsigned muted;

/* The counter wraps at FM1_AUDIO_WRAP; account for one crossing. */
static inline __attribute__((always_inline)) uint32_t since(uint32_t from,
                                                           uint32_t to)
{
    return to >= from ? to - from : to + FM1_AUDIO_WRAP + 1U - from;
}

static inline __attribute__((always_inline)) unsigned pending_halves(void)
{
    return (unsigned)((FM1_ALNK_CON2 >> 4) & ALNK_PENDING_MASK);
}

static inline __attribute__((always_inline)) void acknowledge(unsigned halves)
{
    FM1_ALNK_CON2 = (uint8_t)(halves & ALNK_PENDING_MASK);
}

static inline __attribute__((always_inline)) unsigned free_half(void)
{
    return !(FM1_ALNK_CON0 & ALNK_HALF_FREE);
}

static inline __attribute__((always_inline)) unsigned owns_half(unsigned half)
{
    const unsigned before = free_half();
    const unsigned waiting = pending_halves();
    const unsigned after = free_half();
    return before == half && after == half && !(waiting & (1U << 3));
}

static inline __attribute__((always_inline)) uint32_t mask_audio_irq(void)
{
    uint32_t flags;
    __asm__ volatile("%0 = icfg" : "=r"(flags) :: "memory");
    __asm__ volatile("icfg = %0" :: "r"(flags & ~(1U << 8)) : "memory");
    return flags;
}

static inline __attribute__((always_inline)) void unmask_audio_irq(
    uint32_t flags)
{
    __asm__ volatile("icfg = %0" :: "r"(flags) : "memory");
}

/* No library call in this window: it also runs while Flash is being written. */
static FM1_AUDIO_CODE void commit(const int32_t *source, unsigned half)
{
    for (unsigned n = 0; n < FM1_BLOCK_WORDS; ++n) playback[half][n] = source[n];
    fm1_barrier();
}

static FM1_AUDIO_CODE __attribute__((interrupt(""))) void audio_isr(void)
{
    const unsigned waiting = pending_halves();
    const unsigned rx = waiting & 1U;
    const unsigned half = free_half();
    const uint32_t now = FM1_AUDIO_TICKS;
    unsigned committed = 0;
    uint32_t interval;

    acknowledge(waiting);
    if (!rx || muted) return;

    if (!have_tick) {
        last_tick = now;
        have_tick = 1;
        return;
    }

    interval = since(last_tick, now);
    if (interval < 80000U) {
        ++audio_stats.short_intervals;
        return;
    }
    last_tick = now;
    if (interval < block_ticks - (block_ticks >> 3)) {
        ++audio_stats.short_intervals;
        return;
    }
    if (interval > block_ticks + (block_ticks >> 3)) {
        ++audio_stats.overruns;
        return;
    }

    if (fm1_app_render(block, FM1_BLOCK_FRAMES) != 0) {
        ++audio_stats.overruns;
        return;
    }
    fm1_output_mix(block);

    const uint32_t flags = mask_audio_irq();
    if (owns_half(half) &&
        since(now, FM1_AUDIO_TICKS) < block_ticks - (block_ticks >> 3)) {
        commit(block, half);
        committed = owns_half(half);
    }
    unmask_audio_irq(flags);
    if (!committed) ++audio_stats.overruns;

    /* A second half may have completed while this block was rendered. */
    const unsigned late = pending_halves();
    if (late) {
        acknowledge(late);
        if (late & 1U) ++audio_stats.overruns;
    }

    const uint32_t used = since(now, FM1_AUDIO_TICKS);
    if (used > audio_stats.peak_ticks) audio_stats.peak_ticks = used;
    ++audio_stats.blocks;
}

__attribute__((weak)) void fm1_audio_suspend(void)
{
    const uint32_t flags = fm1_device_irq_save();
    if (muted++ == 0U) {
        for (unsigned half = 0; half < 2; ++half)
            for (unsigned n = 0; n < FM1_BLOCK_WORDS; ++n)
                playback[half][n] = 0;
        fm1_output_reset();
        fm1_device_fence();
    }
    fm1_device_irq_restore(flags);
}

__attribute__((weak)) void fm1_audio_resume(void)
{
    const uint32_t flags = fm1_device_irq_save();
    if (muted && --muted == 0U) have_tick = 0;
    fm1_device_irq_restore(flags);
}

__attribute__((weak)) int fm1_audio_start(void)
{
    if (running) return 0;

    const uint32_t clock = fm1_device_clock();
    if (!clock) return -1;
    block_ticks = (uint32_t)((uint64_t)clock * FM1_BLOCK_FRAMES /
                             AUDIO_HARDWARE_RATE);
    if (!block_ticks) return -1;

    memset(capture, 0, sizeof capture);
    memset(playback, 0, sizeof playback);
    fm1_output_reset();

    FM1_ALNK_CON0 = 0;
    FM1_ALNK_CON1 = 0;
    FM1_ALNK_CON2 = 0;
    FM1_ALNK_CON3 = 0;

    /* Port C carries MCLK, SCLK, LRCLK and the DAC data line. */
    FM1_ALNK_ROUTE &= ~(3U << 6);
    const uint32_t outputs = (1U << 0) | (1U << 1) | (1U << 2) | (1U << 6);
    FM1_GPIO(FM1_PC, FM1_OUT) |= outputs;
    FM1_GPIO(FM1_PC, FM1_DIE) |= 1U << 3;
    FM1_GPIO(FM1_PC, FM1_DIR) =
        (FM1_GPIO(FM1_PC, FM1_DIR) & ~outputs) | (1U << 3);
    FM1_GPIO(FM1_PC, FM1_PD) &= ~(1U << 3);
    FM1_GPIO(FM1_PC, FM1_PU) |= 1U << 3;

    /* 11.2896 MHz / 256 fs gives the fixed 44.1 kHz family rate. */
    FM1_ALNK_CLOCK &= ~(15U << 8);
    FM1_ALNK_CON3 = 3U | (4U << 5);
    FM1_ALNK_CON0 = (1U << 8) | (1U << 7);
    FM1_ALNK_LEN = FM1_BLOCK_WORDS;
    FM1_ALNK_ADR0 = (uint32_t)(uintptr_t)capture;
    FM1_ALNK_ADR3 = (uint32_t)(uintptr_t)playback;
    FM1_ALNK_CON1 = 1U | (1U << 2) | (1U << 3) | (1U << 12) | (1U << 14);
    fm1_device_fence();

    fm1_device_irq_attach(AUDIO_IRQ_INDEX, audio_isr, AUDIO_IRQ_PRIORITY);
    FM1_ALNK_CON0 |= ALNK_TX_START;
    running = 1;
    return 0;
}

void fm1_audio_stats_read(struct fm1_audio_stats *out)
{
    if (!out) return;
    const uint32_t flags = fm1_device_irq_save();
    *out = audio_stats;
    fm1_device_irq_restore(flags);
}
