/* SPDX-License-Identifier: MIT */
/* ST7789 panel on SPI1, with a bounded polling DMA adapter. */
#include "fm1.h"
#include "fm1_device.h"
#include "board.h"

#define LCD_CTRL 0U
#define LCD_DIVIDER 4U
#define LCD_COMMAND 8U
#define LCD_DATA 12U
#define LCD_LENGTH 16U

#define LCD_ENABLE 1U
#define LCD_MASTER (1U << 5)
#define LCD_ACK (1U << 14)
#define LCD_DONE (1U << 15)

#define LCD_CS (1U << 7)   /* PC7 */
#define LCD_DC (1U << 8)   /* PC8 */
#define LCD_PINS (15U << 7)
#define LCD_WIDTH 240U
#define LCD_COLOR_MODE 0x55U

static uint8_t transfer_pending;
static uint32_t transfer_started;
static uint32_t transfer_timeouts;
static uint16_t line_buffer[LCD_WIDTH];

static inline void chip_select(int active)
{
    if (active)
        FM1_GPIO(FM1_PC, FM1_OUT) &= ~(LCD_DC | LCD_CS);
    else
        FM1_GPIO(FM1_PC, FM1_OUT) |= LCD_CS;
}

static inline uint16_t swapped(uint16_t value)
{
    return (uint16_t)((value >> 8) | (value << 8));
}

static unsigned controller_ready(void)
{
    return (FM1_SPI1(LCD_CTRL) & LCD_DONE) != 0U;
}

static void abort_transfer(void)
{
    FM1_SPI1(LCD_CTRL) = 0U;
    fm1_barrier();
    FM1_SPI1(LCD_LENGTH) = 0U;
    chip_select(0);
    FM1_SPI1(LCD_CTRL) = LCD_ENABLE | LCD_MASTER | LCD_ACK;
    ++transfer_timeouts;
}

static void await_completion(void)
{
    for (unsigned spins = 4000000U; spins; --spins) {
        if (controller_ready()) {
            FM1_SPI1(LCD_CTRL) |= LCD_ACK;
            return;
        }
    }
    abort_transfer();
}

static void write_command(uint8_t command)
{
    FM1_GPIO(FM1_PC, FM1_OUT) &= ~(LCD_DC | LCD_CS);
    FM1_SPI1(LCD_CTRL) |= LCD_ACK;
    FM1_SPI1(LCD_COMMAND) = command;
}

static void write_data(const void *data, uint32_t bytes)
{
    FM1_GPIO(FM1_PC, FM1_OUT) =
        (FM1_GPIO(FM1_PC, FM1_OUT) | LCD_DC) & ~LCD_CS;
    FM1_SPI1(LCD_CTRL) |= LCD_ACK;
    fm1_barrier();
    FM1_SPI1(LCD_DATA) = (uint32_t)(uintptr_t)data;
    FM1_SPI1(LCD_LENGTH) = bytes;
}

static void finish_transfer(void)
{
    if (!transfer_pending) return;
    await_completion();
    chip_select(0);
    transfer_pending = 0;
}

static void command_with_wait(uint8_t command)
{
    write_command(command);
    await_completion();
    chip_select(0);
}

static void data_with_wait(const void *data, unsigned bytes)
{
    if (!bytes) return;
    write_data(data, bytes);
    await_completion();
    chip_select(0);
}

static void set_window(unsigned x, unsigned y, unsigned w, unsigned h)
{
    const uint8_t edges[8] = {
        (uint8_t)(x >> 8), (uint8_t)x,
        (uint8_t)((x + w - 1U) >> 8), (uint8_t)(x + w - 1U),
        (uint8_t)(y >> 8), (uint8_t)y,
        (uint8_t)((y + h - 1U) >> 8), (uint8_t)(y + h - 1U)
    };
    command_with_wait(0x2a);
    data_with_wait(edges, 4);
    command_with_wait(0x2b);
    data_with_wait(edges + 4, 4);
    command_with_wait(0x2c);
}

static int rectangle_ok(unsigned x, unsigned y, unsigned w, unsigned h,
                        const void *pixels, size_t count)
{
    return pixels && w && h && x < LCD_WIDTH && y < LCD_WIDTH &&
           w <= LCD_WIDTH - x && h <= LCD_WIDTH - y && count >= (size_t)w * h;
}

static void hardware_init(void)
{
    /* PA2 is held low while the controller initialises. */
    FM1_GPIO(FM1_PA, FM1_OUT) &= ~(1U << 2);
    FM1_GPIO(FM1_PA, FM1_DIR) &= ~(1U << 2);

    FM1_GPIO(FM1_PC, FM1_OUT) =
        (FM1_GPIO(FM1_PC, FM1_OUT) & ~LCD_PINS) | LCD_CS;
    FM1_GPIO(FM1_PC, FM1_DIR) &= ~LCD_PINS;
    FM1_GPIO(FM1_PC, FM1_DIE) |= LCD_PINS;
    FM1_GPIO(FM1_PC, FM1_PU) &= ~LCD_PINS;
    FM1_GPIO(FM1_PC, FM1_PD) &= ~LCD_PINS;

    FM1_IOMAP |= 1U << 4;
    FM1_SPI1(LCD_DIVIDER) = 4;
    FM1_SPI1(LCD_CTRL) = LCD_ENABLE | LCD_MASTER | LCD_ACK;
}

static void spin_delay(unsigned ms)
{
    while (ms--)
        for (volatile unsigned n = 0; n < 25000U; ++n) {}
}

/* Command stream: a command byte, a length, then that many payload bytes.
 * Command 0x45 is a delay in milliseconds instead of a register write. */
static const uint8_t init_sequence[] = {
    0x11, 0, 0x45, 120,
    0x2a, 4, 0, 0, 0, 0xef,
    0x2b, 4, 0, 0x28, 1, 0x17,
    0xb2, 5, 0x0c, 0x0c, 0x0c, 0, 0x33,
    0x20, 0,
    0xb7, 1, 0x56,
    0xbb, 1, 0x18,
    0xc0, 1, 0x2c,
    0xc2, 1, 1,
    0xc3, 1, 0x1f,
    0xc4, 1, 0x20,
    0xc6, 1, 0x0f,
    0xd0, 2, 0xa6, 0xa1,
    0xe0, 14, 0xd0, 0x0d, 0x14, 0x0b, 0x0b, 0x07, 0x3a,
        0x44, 0x50, 0x08, 0x13, 0x13, 0x2d, 0x32,
    0xe1, 14, 0xd0, 0x0d, 0x14, 0x0b, 0x0b, 0x07, 0x3a,
        0x44, 0x50, 0x08, 0x13, 0x13, 0x2d, 0x32,
    0x36, 1, 0,
    0x3a, 1, LCD_COLOR_MODE,
    0xe7, 1, 0,
    0x51, 1, 255,
    0x21, 0
};

void fm1_screen_init(void)
{
    finish_transfer();
    hardware_init();
    FM1_SPI1(LCD_DIVIDER) = 4;

    command_with_wait(0x28);
    spin_delay(100);

    for (unsigned i = 0; i < sizeof init_sequence;) {
        const unsigned command = init_sequence[i++];
        const unsigned length = init_sequence[i++];
        if (command == 0x45) {
            spin_delay(length);
            continue;
        }
        uint8_t payload[16];
        for (unsigned n = 0; n < length; ++n) payload[n] = init_sequence[i + n];
        command_with_wait((uint8_t)command);
        data_with_wait(payload, length);
        i += length;
    }

    fm1_screen_fill(0, 0, LCD_WIDTH, LCD_WIDTH, 0);
    command_with_wait(0x29);
}

void fm1_screen_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t color)
{
    if (!w || !h || x >= LCD_WIDTH || y >= LCD_WIDTH) return;
    if (w > LCD_WIDTH - x) w = LCD_WIDTH - x;
    if (h > LCD_WIDTH - y) h = LCD_WIDTH - y;

    finish_transfer();
    unsigned remaining = w * h;
    const unsigned batch = remaining < LCD_WIDTH ? remaining : LCD_WIDTH;
    for (unsigned n = 0; n < batch; ++n) line_buffer[n] = swapped(color);

    set_window(x, y, w, h);
    while (remaining) {
        const unsigned count = remaining < batch ? remaining : batch;
        data_with_wait(line_buffer, 2U * count);
        remaining -= count;
    }
}

int fm1_screen_blit(unsigned x, unsigned y, unsigned w, unsigned h,
                    const uint16_t *pixels, size_t count)
{
    if (!rectangle_ok(x, y, w, h, pixels, count)) return -1;
    finish_transfer();
    const uint32_t before = transfer_timeouts;
    set_window(x, y, w, h);

    for (unsigned remaining = w * h; remaining;) {
        const unsigned batch = remaining < LCD_WIDTH ? remaining : LCD_WIDTH;
        for (unsigned n = 0; n < batch; ++n) line_buffer[n] = swapped(*pixels++);
        data_with_wait(line_buffer, 2U * batch);
        remaining -= batch;
        if (transfer_timeouts != before) return -2;
    }
    return transfer_timeouts == before ? 0 : -2;
}

int fm1_screen_pending(void)
{
    if (!transfer_pending) return 0;
    if (controller_ready()) {
        finish_transfer();
        return 0;
    }
    if ((uint32_t)(fm1_millis() - transfer_started) < 20U) return 1;
    abort_transfer();
    transfer_pending = 0;
    return -2;
}

int fm1_screen_submit(unsigned x, unsigned y, unsigned w, unsigned h,
                      uint16_t *pixels, size_t count)
{
    if (!rectangle_ok(x, y, w, h, pixels, count) ||
        (size_t)w * h > FM1_SCREEN_DMA_PIXELS)
        return -1;
    const int busy = fm1_screen_pending();
    if (busy) return busy;

    const uint32_t before = transfer_timeouts;
    set_window(x, y, w, h);
    if (transfer_timeouts != before) return -2;

    for (unsigned n = 0; n < w * h; ++n) pixels[n] = swapped(pixels[n]);
    transfer_started = fm1_millis();
    transfer_pending = 1;
    write_data(pixels, 2U * w * h);
    return 0;
}
