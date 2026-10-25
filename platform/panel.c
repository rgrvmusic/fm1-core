/* SPDX-License-Identifier: MIT */
/* Key matrix, encoders, LEDs and analogue controls. */
#include <string.h>

#include "fm1.h"
#include "fm1_device.h"
#include "board.h"

enum { MATRIX_COLUMNS = 11, MATRIX_KEYS = 41, ENCODER_COUNT = 7 };

/* Each entry is (column << 4) | bit for the row input that samples the key. */
static const uint8_t key_location[MATRIX_KEYS] FM1_AUDIO_DATA = {
    0x04, 0x14, 0x61, 0x41, 0x21, 0x01, 0x92, 0x82, 0x71, 0x51, 0x31, 0x11,
    0xa2, 0x72, 0x34, 0x24, 0x54, 0x44, 0x74, 0x64, 0x84, 0x94, 0xa4, 0x03,
    0x13, 0x23, 0x33, 0x43, 0x53, 0x63, 0x73, 0x83, 0x93, 0xa3, 0x02, 0x12,
    0x22, 0x32, 0x42, 0x62, 0x52
};

/* Quadrature switch pairs, sampled in the same matrix pass. */
static const uint8_t encoder_location[ENCODER_COUNT][2] FM1_AUDIO_DATA = {
    {0x00, 0x10}, {0x20, 0x30}, {0x81, 0x91}, {0x80, 0x90},
    {0x60, 0x70}, {0x40, 0x50}, {0x05, 0x15}
};

/* +1 or -1 for the valid four-state transitions; diagonals are ignored. */
static const int8_t quadrature_step[16] FM1_AUDIO_DATA = {
    0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0
};

struct scanner {
    volatile uint32_t notes;
    volatile uint32_t buttons;
    volatile int16_t detents[ENCODER_COUNT];
    uint8_t row[MATRIX_COLUMNS];
    uint8_t pressed[MATRIX_KEYS];
    uint8_t released[MATRIX_KEYS];
    uint8_t phase[ENCODER_COUNT];
    uint8_t rest[ENCODER_COUNT];
    uint16_t steady[ENCODER_COUNT];
    int8_t edges[ENCODER_COUNT];
    uint8_t column;
    uint8_t queued;
    uint8_t stalled;
    uint8_t started;
};

static struct scanner scan;
static volatile uint8_t led_shadow[MATRIX_COLUMNS];
static uint8_t scan_word[4] __attribute__((aligned(4)));

/* Select one column by pulling its line low; the boot straps on columns 0 and
 * 1 need their companion bit driven as well. */
static inline __attribute__((always_inline)) void begin_column(unsigned column)
{
    uint32_t pattern = 65535U ^ (1U << column);
    if (column < 2) pattern ^= 1U << (11U + column);
    scan_word[0] = (uint8_t)(pattern >> 8);
    scan_word[1] = (uint8_t)pattern;
    FM1_SPI2(0) |= 1U << 14;
    fm1_barrier();
    FM1_SPI2(12) = (uint32_t)(uintptr_t)scan_word;
    FM1_SPI2(16) = 2;
    scan.queued = (uint8_t)column;
}

/* Apply the LED column and keep the row inputs in their idle state. */
static inline __attribute__((always_inline)) void show_column(unsigned column)
{
    const uint32_t pa_group = (1U << 9) | (1U << 10);
    const uint32_t ph_group = (1U << 6) | (1U << 9);
    const uint8_t leds = led_shadow[column];

    FM1_GPIO(FM1_PA, FM1_OUT) &= ~pa_group;
    FM1_GPIO(FM1_PH, FM1_OUT) &= ~ph_group;
    FM1_GPIO(FM1_PA, FM1_OUT) |= 1U << 1;
    FM1_GPIO(FM1_PA, FM1_OUT) &= ~(1U << 1);
    FM1_GPIO(FM1_PA, FM1_OUT) |= (uint32_t)(leds & 6U) << 8;
    FM1_GPIO(FM1_PH, FM1_OUT) |=
        (uint32_t)((leds & 8U) << 3) | ((leds & 16U) << 5);
    scan.column = (uint8_t)column;
}

static inline __attribute__((always_inline)) unsigned contact(unsigned location)
{
    return (scan.row[location >> 4] >> (location & 15)) & 1U;
}

static FM1_AUDIO_CODE void decode_matrix(void)
{
    for (unsigned key = 0; key < MATRIX_KEYS; ++key) {
        const unsigned down = contact(key_location[key]);
        const uint32_t mask = 1U << (key < 14U ? key : key - 14U);
        volatile uint32_t *const level =
            key < 14U ? &scan.buttons : &scan.notes;

        if (down) {
            scan.released[key] = 0;
            if (scan.pressed[key] < 2U && ++scan.pressed[key] == 2U)
                *level |= mask;
        } else {
            scan.pressed[key] = 0;
            if (scan.released[key] < 8U && ++scan.released[key] == 8U)
                *level &= ~mask;
        }
    }

    for (unsigned index = 0; index < ENCODER_COUNT; ++index) {
        const unsigned now =
            (contact(encoder_location[index][0]) << 1) |
            contact(encoder_location[index][1]);
        const unsigned was = scan.phase[index];

        if (was > 3U) {
            /* First sample after power-on has no known direction. */
            scan.phase[index] = scan.rest[index] = (uint8_t)now;
            continue;
        }
        if (now != was) {
            scan.steady[index] = 0;
            if ((now ^ was) == 3U) {
                scan.edges[index] = 0;
            } else {
                int travel = scan.edges[index] + quadrature_step[was * 4U + now];
                scan.edges[index] =
                    (int8_t)(travel > 8 ? 8 : travel < -8 ? -8 : travel);
            }
            scan.phase[index] = (uint8_t)now;
            continue;
        }
        if (scan.steady[index] < 900U) ++scan.steady[index];
        if (scan.steady[index] == 900U) {
            scan.rest[index] = (uint8_t)now;
            scan.edges[index] = 0;
        }
        if (now == scan.rest[index]) {
            int steps = scan.detents[index] + scan.edges[index] / 4;
            if (steps > 32767) steps = 32767;
            if (steps < -32768) steps = -32768;
            scan.detents[index] = (int16_t)steps;
            scan.edges[index] = 0;
        }
    }
}

/* Called from the panel interrupt. The SPI transfer started in the previous
 * tick must already be complete; waiting here would stall the interrupt. */
static FM1_AUDIO_CODE void input_tick(void)
{
    if (!(FM1_SPI2(0) & (1U << 15))) {
        if (++scan.stalled == 20U) {
            FM1_SPI2(0) = 0;
            fm1_barrier();
            FM1_SPI2(16) = 0;
            FM1_SPI2(0) = (1U << 14) | (1U << 5) | 1U;
            begin_column(scan.queued);
            scan.stalled = 0;
        }
        return;
    }
    scan.stalled = 0;

    const unsigned column = scan.column;
    const uint32_t port_a = FM1_GPIO(FM1_PA, FM1_IN);
    const uint32_t port_b = FM1_GPIO(FM1_PB, FM1_IN);
    scan.row[column] =
        (uint8_t)(~((port_a & 1U) | ((port_a >> 4) & 30U) |
                    ((port_b >> 2) & 32U)) & 63U);

    const unsigned next = scan.queued;
    show_column(next);
    begin_column(next == MATRIX_COLUMNS - 1U ? 0U : next + 1U);
    if (column == MATRIX_COLUMNS - 1U) decode_matrix();
}

static void input_init(void)
{
    memset(&scan, 0, sizeof scan);
    memset(scan.phase, 255, sizeof scan.phase);

    const uint32_t inputs = 1U | (15U << 5);           /* PA0, PA5..PA8 */
    const uint32_t outputs = (1U << 1) | (1U << 3) | (1U << 4) | (3U << 9);
    const uint32_t column_outputs = (1U << 6) | (1U << 9);

    FM1_GPIO(FM1_PA, FM1_DIE) |= inputs | outputs;
    FM1_GPIO(FM1_PB, FM1_DIE) |= 1U << 7;
    FM1_GPIO(FM1_PH, FM1_DIE) |= column_outputs;

    FM1_GPIO(FM1_PA, FM1_OUT) &= ~outputs;
    FM1_GPIO(FM1_PH, FM1_OUT) &= ~column_outputs;

    FM1_GPIO(FM1_PA, FM1_DIR) =
        (FM1_GPIO(FM1_PA, FM1_DIR) | inputs) & ~outputs;
    FM1_GPIO(FM1_PB, FM1_DIR) |= 1U << 7;
    FM1_GPIO(FM1_PH, FM1_DIR) &= ~column_outputs;

    FM1_GPIO(FM1_PA, FM1_PU) =
        (FM1_GPIO(FM1_PA, FM1_PU) | inputs) & ~outputs;
    FM1_GPIO(FM1_PA, FM1_PD) &= ~(inputs | outputs);
    FM1_GPIO(FM1_PB, FM1_PU) |= 1U << 7;
    FM1_GPIO(FM1_PB, FM1_PD) &= ~(1U << 7);
    FM1_GPIO(FM1_PH, FM1_PU) &= ~column_outputs;
    FM1_GPIO(FM1_PH, FM1_PD) &= ~column_outputs;

    FM1_GPIO(FM1_PA, FM1_HD0) |= 3U << 9;
    FM1_GPIO(FM1_PA, FM1_HD) |= 3U << 9;
    FM1_GPIO(FM1_PH, FM1_HD0) |= column_outputs;
    FM1_GPIO(FM1_PH, FM1_HD) |= column_outputs;

    FM1_IOMAP |= 1U << 17;
    FM1_SPI2(4) = 29; /* clock divider */
    FM1_SPI2(0) = (1U << 14) | (1U << 5) | 1U;
    begin_column(0);

    unsigned wait = 100000U;
    while (!(FM1_SPI2(0) & (1U << 15)) && --wait) {}
    if (wait) {
        show_column(0);
        begin_column(1);
    }
}

uint8_t fm1_io_trs_midi_enabled(void)
{
    /* The FM-1 board fits the TRS MIDI receiver. */
    return 1U;
}

void fm1_controls_init(void)
{
    if (scan.started) return;

    /* PB1 and PB6 are ADC inputs, not digital GPIO. */
    const uint32_t analogue = (1U << 1) | (1U << 6);
    FM1_GPIO(FM1_PB, FM1_DIR) |= analogue;
    FM1_GPIO(FM1_PB, FM1_DIE) &= ~analogue;
    FM1_GPIO(FM1_PB, FM1_PU) &= ~analogue;
    FM1_GPIO(FM1_PB, FM1_PD) &= ~analogue;

    input_init();
    fm1_device_start(input_tick);
    scan.started = 1U;
}

void fm1_controls_read(struct fm1_controls *state)
{
    if (!state) return;
    struct fm1_controls snapshot;
    const uint32_t irq = fm1_device_irq_save();

    snapshot.keys = scan.notes;
    snapshot.buttons = scan.buttons;
    for (unsigned index = ENCODER_COUNT; index-- > 0;) {
        snapshot.encoders[index] = scan.detents[index];
        scan.detents[index] = 0;
    }
    fm1_device_irq_restore(irq);

    snapshot.knob = (uint16_t)fm1_device_adc(4U);
    snapshot.battery = (uint16_t)fm1_device_adc(3U);
    *state = snapshot;
}

void fm1_led_set(unsigned key, int on)
{
    if (key >= MATRIX_KEYS) return;
    const unsigned location = key_location[key];
    const unsigned column = location >> 4;
    const uint8_t mask = (uint8_t)(1U << (location & 15));
    const uint8_t current = led_shadow[column];
    led_shadow[column] = (uint8_t)(on ? current | mask : current & ~mask);
}
