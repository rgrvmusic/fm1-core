/* SPDX-License-Identifier: MIT */
/* Board services supplied by the fixed FM-1 runtime. */
#ifndef FM1_DEVICE_H
#define FM1_DEVICE_H

#include "fm1.h"

/* Start the board timebase and a periodic input scan. The scan callback runs
 * in the short input interrupt, not in task context. */
void fm1_device_start(void (*scan)(void));

/* Wrapping millisecond timebase and a task-context delay. */
uint32_t fm1_device_ticks(void);
void fm1_device_delay(unsigned ms);

/* Memory and cache barrier for code that shares buffers with an interrupt. */
void fm1_device_fence(void);

/* Raw ADC channel; the board assigns channels 3 (battery) and 4 (master). */
int fm1_device_adc(unsigned channel);
void fm1_device_adc_register(unsigned channel);

/* Control-task interrupt masking. Keep the critical section short: no
 * allocation, Flash access or drawing while interrupts are held. */
uint32_t fm1_device_irq_save(void);
void fm1_device_irq_restore(uint32_t state);

/* Route a fixed-board interrupt to a handler at the given priority. */
void fm1_device_irq_attach(unsigned index, void (*handler)(void), unsigned priority);

/* Fixed clock rate used to derive audio block timing. */
uint32_t fm1_device_clock(void);

/* Board wiring: return nonzero when the TRS MIDI receiver is fitted. */
uint8_t fm1_io_trs_midi_enabled(void);

/* USB enumeration state and the recovery/updater entry. */
int fm1_device_usb_connected(void);
void fm1_device_recovery(void);

/* Read-only mapping for immutable sample storage. Returns NULL when the
 * range is outside the mapped user partition. The caller owns sample and
 * cache lifetimes and must pause its engine across format changes. Writes
 * through the SDK invalidate the hardware caches. */
const uint8_t *fm1_flash_map(uint32_t offset, size_t bytes);
int fm1_flash_invalidate(uint32_t offset, size_t bytes);

#endif
