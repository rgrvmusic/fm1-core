/* SPDX-License-Identifier: MIT */
/* AC791N/WL82 register access used by the FM-1 platform layer. */
#ifndef FM1_PLATFORM_BOARD_H
#define FM1_PLATFORM_BOARD_H

#include <stdint.h>

/* General-purpose I/O: one 32-bit bank of 16 registers per port. */
#define FM1_PA 0U
#define FM1_PB 1U
#define FM1_PC 2U
#define FM1_PH 7U

#define FM1_OUT 0U   /* output data */
#define FM1_IN 4U    /* input data */
#define FM1_DIR 8U   /* direction, 1 = input */
#define FM1_DIE 12U  /* digital input enable */
#define FM1_PU 16U   /* pull-up */
#define FM1_PD 20U   /* pull-down */
#define FM1_HD0 24U  /* high drive, first group */
#define FM1_HD 28U   /* high drive, second group */

#define FM1_GPIO(port, reg) \
    (*(volatile uint32_t *)(uintptr_t)(0x50000U + 64U * (port) + (reg)))

/* Pin multiplexing: bit 4 enables SPI1, bit 17 enables SPI2. */
#define FM1_IOMAP (*(volatile uint32_t *)0x51020U)

/* SPI1 drives the display; SPI2 clocks the key matrix. */
#define FM1_SPI1(offset) (*(volatile uint32_t *)(uintptr_t)(0x11d00U + (offset)))
#define FM1_SPI2(offset) (*(volatile uint32_t *)(uintptr_t)(0x11e00U + (offset)))

/* ALNK0 audio DMA. The DAC runs from port C. */
#define FM1_ALNK_CON0 (*(volatile uint16_t *)0x12e00U)
#define FM1_ALNK_CON1 (*(volatile uint16_t *)0x12e04U)
#define FM1_ALNK_CON2 (*(volatile uint8_t *)0x12e08U)
#define FM1_ALNK_CON3 (*(volatile uint8_t *)0x12e0cU)
#define FM1_ALNK_ADR0 (*(volatile uint32_t *)0x12e10U)
#define FM1_ALNK_ADR3 (*(volatile uint32_t *)0x12e1cU)
#define FM1_ALNK_LEN (*(volatile uint16_t *)0x12e20U)
#define FM1_ALNK_ROUTE (*(volatile uint32_t *)0x51030U)
#define FM1_ALNK_CLOCK (*(volatile uint32_t *)0x10014U)

/* Free-running sample counter and its wrap value. */
#define FM1_AUDIO_TICKS (*(volatile uint32_t *)0x1eef0f0U)
#define FM1_AUDIO_WRAP (*(volatile uint32_t *)0x1eef0f4U)

/* Board timers. Timer 4 counts at the system clock; timer 5 scans keys. */
#define FM1_TIMER(offset) (*(volatile uint32_t *)(uintptr_t)(0x10800U + (offset)))
#define FM1_PANEL_IRQ(offset) (*(volatile uint32_t *)(uintptr_t)(0x10900U + (offset)))

static inline void fm1_barrier(void)
{
    __asm__ volatile("csync" ::: "memory");
}

#endif
