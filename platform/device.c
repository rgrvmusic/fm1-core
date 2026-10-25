/* SPDX-License-Identifier: MIT */
/* Board timebase and periodic input interrupt. */
#include "fm1_device.h"
#include "board.h"

#define TIMER4_CONTROL 0x00U
#define TIMER4_COUNT 0x04U
#define TIMER4_PERIOD 0x08U
#define TIMER4_RUN_FREE 0x4009U

#define PANEL_CONTROL 0x00U
#define PANEL_CLEAR 0x04U
#define PANEL_INTERVAL 0x08U
#define PANEL_IRQ_BIT 0x4000U
#define PANEL_ENABLE 0x4019U
#define PANEL_IRQ_INDEX 63U
#define PANEL_IRQ_PRIORITY 5U

static void (*panel_handler)(void);
static uint8_t board_started;

/* The interrupt is acknowledged before the scan so the source cannot retrigger
 * while the matrix state is being sampled. */
static FM1_AUDIO_CODE __attribute__((interrupt(""))) void panel_isr(void)
{
    FM1_PANEL_IRQ(PANEL_CONTROL) |= PANEL_IRQ_BIT;
    if (panel_handler) panel_handler();
}

void fm1_device_start(void (*scan)(void))
{
    if (board_started) return;
    board_started = 1U;

    /* Timer 4 is left free-running as the audio interval timebase. */
    FM1_TIMER(TIMER4_CONTROL) = 0U;
    FM1_TIMER(TIMER4_COUNT) = 0U;
    FM1_TIMER(TIMER4_PERIOD) = 0xffffffffU;
    FM1_TIMER(TIMER4_CONTROL) = TIMER4_RUN_FREE;

    fm1_device_adc_register(3U); /* battery divider on PB1 */
    fm1_device_adc_register(4U); /* master potentiometer on PB6 */

    panel_handler = scan;
    FM1_PANEL_IRQ(PANEL_CONTROL) = PANEL_IRQ_BIT;
    FM1_PANEL_IRQ(PANEL_CLEAR) = 0U;
    FM1_PANEL_IRQ(PANEL_INTERVAL) = 600U;
    fm1_device_irq_attach(PANEL_IRQ_INDEX, panel_isr, PANEL_IRQ_PRIORITY);
    FM1_PANEL_IRQ(PANEL_CONTROL) = PANEL_ENABLE;
}
