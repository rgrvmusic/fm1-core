/* SPDX-License-Identifier: MIT */
/* Master gain and external audio sources for one rendered block. */
#include "mixer.h"

#include "fm1_audio_io.h"
#include "fm1_engine_control.h"

#define MASTER_MAX 127U
#define MASTER_STEP 8
#define LIMIT_24 8388607
#define CLIP_24 8388608

static unsigned current_gain;
static int32_t usb_block[FM1_BLOCK_WORDS] __attribute__((aligned(32)));
static int16_t bluetooth_block[FM1_BLOCK_WORDS] __attribute__((aligned(32)));

/* Applications that drive the DAC themselves may replace both functions. */
__attribute__((weak)) void fm1_output_reset(void)
{
    current_gain = 0;
}

/* Clamp to signed 24-bit. The bias makes one unsigned test cover both ends,
 * including the int64 extremes. */
static inline __attribute__((always_inline)) int32_t clamp24(int64_t value)
{
    if (((uint64_t)value + 8388608U) >> 24)
        return value < 0 ? -8388608 : 8388607;
    return (int32_t)value;
}

__attribute__((weak)) FM1_AUDIO_CODE void fm1_output_mix(int32_t *output)
{
    if (!output) return;

    unsigned target = fm1_engine_output_level();
    if (target > MASTER_MAX) target = MASTER_MAX;
    int change = (int)target - (int)current_gain;
    if (change > MASTER_STEP) change = MASTER_STEP;
    if (change < -MASTER_STEP) change = -MASTER_STEP;
    current_gain = (unsigned)((int)current_gain + change);

    uint16_t usb_gain[2] = {0, 0};
    const int usb_frames =
        fm1_usb_audio_read(usb_block, FM1_BLOCK_FRAMES, usb_gain);
    const unsigned left = ((unsigned)usb_gain[0] * current_gain) >> 7;
    const unsigned right = ((unsigned)usb_gain[1] * current_gain) >> 7;

    /* The engine block is scaled first. USB audio joins it before clipping;
     * a reset USB stream mutes the block instead of leaking stale samples. */
    for (unsigned n = 0; n < FM1_BLOCK_WORDS; ++n) {
        int64_t sample = ((int64_t)output[n] * current_gain) >> 7;
        if (usb_frames == FM1_BLOCK_FRAMES)
            sample += ((int64_t)usb_block[n] * ((n & 1U) ? right : left)) >> 7;
        output[n] = usb_frames == -2 ? 0 : clamp24(sample);
    }

    if (fm1_bluetooth_audio_read(bluetooth_block, FM1_BLOCK_FRAMES) ==
        FM1_BLOCK_FRAMES) {
        /* output is already 24-bit. A signed 16-bit sample times at most 254
         * fits in 25 bits, so this stage stays in 32-bit arithmetic. */
        for (unsigned n = 0; n < FM1_BLOCK_WORDS; ++n) {
            const int32_t mixed =
                output[n] + (int32_t)bluetooth_block[n] *
                               (int32_t)(current_gain * 2U);
            output[n] = mixed > LIMIT_24 ? LIMIT_24
                      : mixed < -CLIP_24 ? -CLIP_24
                                         : mixed;
        }
    }

    (void)fm1_usb_audio_write(output, FM1_BLOCK_FRAMES);
}
