/* SPDX-License-Identifier: MIT */
/* Minimal FM-1 application: panel scan, status display and a sine output.
 *
 * It replaces fm1_app_render() and fm1_output_mix() from the platform layer,
 * showing the two points an alternative audio engine can take over. The
 * knob sets the level; any key or button opens the gate.
 */
#include "fm1.h"
#include "fm1_app.h"

#define TONE_HZ 440U
/* 440 * 2^32 / 44100, truncated. */
#define PHASE_STEP 42849264U

static const int16_t sine_table[64] = {
         0,   3212,   6393,   9512,  12539,  15446,  18204,  20787,
     23170,  25329,  27245,  28898,  30273,  31356,  32137,  32609,
     32767,  32609,  32137,  31356,  30273,  28898,  27245,  25329,
     23170,  20787,  18204,  15446,  12539,   9512,   6393,   3212,
         0,  -3212,  -6393,  -9512, -12539, -15446, -18204, -20787,
    -23170, -25329, -27245, -28898, -30273, -31356, -32137, -32609,
    -32767, -32609, -32137, -31356, -30273, -28898, -27245, -25329,
    -23170, -20787, -18204, -15446, -12539,  -9512,  -6393,  -3212,
};

static volatile uint16_t master;
static volatile uint8_t gate;
static uint32_t phase;

int fm1_app_render(int32_t *output, unsigned frames)
{
    if (!output || frames != FM1_BLOCK_FRAMES) return -1;
    const uint32_t knob = master;
    const int32_t level = (int32_t)(((uint32_t)knob * (1U << 20)) / 1023U);

    for (unsigned frame = 0; frame < frames; ++frame) {
        const uint32_t position = phase;
        phase = position + PHASE_STEP;
        const int32_t sample = gate
            ? ((int32_t)sine_table[position >> 26] * level) >> 15
            : 0;
        output[2U * frame] = sample;
        output[2U * frame + 1U] = sample;
    }
    return 0;
}

FM1_AUDIO_CODE void fm1_output_mix(int32_t *output)
{
    (void)output; /* the sine is already at the right level */
}

static void draw_level(void)
{
    const unsigned width = 240U * master / 1023U;
    fm1_screen_fill(0, 100, 240, 20, 0x4208);
    if (width) fm1_screen_fill(0, 100, width, 20, 0x07e0);
}

void groove_screen_init(void)
{
    fm1_controls_init();
    fm1_screen_init();
    fm1_screen_fill(0, 0, 240, 240, 0x0000);
    fm1_screen_fill(0, 40, 240, 24, 0x001f);
    draw_level();
}

void groove_screen_control(void)
{
    static uint32_t last_read;
    static uint16_t last_knob = 0xffffU;
    struct fm1_controls controls;

    const uint32_t now = fm1_millis();
    if ((uint32_t)(now - last_read) < 20U) return;
    last_read = now;

    fm1_controls_read(&controls);
    master = controls.knob;
    gate = (controls.keys || controls.buttons) ? 1U : 0U;

    const int movement = (int)controls.knob - (int)last_knob;
    if (movement > 16 || movement < -16) {
        last_knob = controls.knob;
        draw_level();
    }

    for (unsigned button = 0; button < 14U; ++button)
        fm1_led_set(button, (controls.buttons >> button) & 1U);
    for (unsigned key = 0; key < 27U; ++key)
        fm1_led_set(14U + key, (controls.keys >> key) & 1U);
}
