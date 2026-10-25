/* SPDX-License-Identifier: MIT */
/* FM-1 platform API: MIDI, Bluetooth, audio timing, panel, screen and Flash.
 *
 * The fixed runtime is supplied as a binary SDK (sdk/lib/libfm1_engine.a).
 * This header is an independent, permissively licensed description of the
 * interface that binary exposes; it is not derived from the vendor's
 * application sources.
 */
#ifndef FM1_H
#define FM1_H

#include <stddef.h>
#include <stdint.h>

#define FM1_API_VERSION 1U

#include "fm1_block.h"

#ifdef FM1_HOST_TEST
#define FM1_AUDIO_CODE
#define FM1_AUDIO_DATA
#else
/* Audio code and every callee must remain safe while Flash is programmed. */
#define FM1_AUDIO_CODE __attribute__((section(".volatile_ram_code"), noinline, used))
/* Initialised tables used from the audio interrupt. Large zeroed working
 * buffers belong in ordinary internal BSS instead. */
#define FM1_AUDIO_DATA __attribute__((section(".volatile_ram.fm1"), aligned(4)))
#endif

/* --- MIDI --------------------------------------------------------------- */

/* Queue one complete raw MIDI message for USB/BLE output.
 * 0 queued, -1 invalid or no transport ring, -2 busy/full (retry later).
 * Success is not a delivery receipt. Control-task context only. */
int fm1_midi_send(const uint8_t *message, size_t bytes);

enum { FM1_MIDI_DIN, FM1_MIDI_USB, FM1_MIDI_BLE };

unsigned fm1_midi_output_ports(void); /* physically supported outputs */
unsigned fm1_midi_input_ports(void);  /* physically supported inputs */
unsigned fm1_midi_ready_ports(void);  /* currently available outputs */

/* Per-port send with independent bounded queues; a slow port cannot block
 * another. Complete messages of at most 128 bytes. 0 queued, -1 offline or
 * invalid, -2 full. */
int fm1_midi_send_port(unsigned port, const uint8_t *message, size_t bytes);

/* Routing callback for a complete short message. The default feeds local
 * input. Do not block, allocate, draw, touch Flash or edit audio here; queue
 * deferred work instead. */
void fm1_app_midi_received(unsigned source, const uint8_t *message, size_t bytes);

/* Valid only inside fm1_app_sysex; the source of the frame being handled. */
unsigned fm1_midi_input_source(void);

/* Feed local keys/controllers into the instrument's MIDI input. Complete
 * short messages only. 0 queued, -1 invalid or overflow. Control task only. */
int fm1_midi_post(const uint8_t *message, size_t bytes);

/* --- Bluetooth ---------------------------------------------------------- */

enum { FM1_BT_AUDIO = 1, FM1_BT_MIDI = 2 };

struct fm1_bluetooth_status {
    uint8_t features, ready, audio_connected, audio_playing;
};

/* A zero feature mask disables both links and discovery while leaving the
 * stack initialised. 0 accepted, -1 invalid, -2 busy. No Flash write. */
int fm1_bluetooth_configure(unsigned features);
void fm1_bluetooth_get_status(struct fm1_bluetooth_status *status);

enum fm1_ble_role { FM1_BLE_PERIPHERAL, FM1_BLE_CENTRAL };
enum fm1_ble_state {
    FM1_BLE_OFF, FM1_BLE_IDLE, FM1_BLE_ADVERTISING, FM1_BLE_SCANNING,
    FM1_BLE_CONNECTING, FM1_BLE_DISCOVERING, FM1_BLE_CONNECTED,
    FM1_BLE_READY, FM1_BLE_DISCONNECTING
};
enum fm1_ble_error {
    FM1_BLE_OK, FM1_BLE_STACK_ERROR, FM1_BLE_TIMEOUT, FM1_BLE_NOT_MIDI,
    FM1_BLE_GATT_ERROR, FM1_BLE_LINK_LOST
};

#define FM1_BLE_MAX_DEVICES 8U

struct fm1_ble_device {
    char name[25];
    uint8_t address[6], address_type;
    int8_t rssi;
    uint8_t midi; /* MIDI service advertised; discovery is still required */
};

struct fm1_ble_status {
    uint8_t role, state, count, error, pending;
};

/* Asynchronous BLE MIDI commands: 0 queued, -1 invalid, -2 busy, -3 not ready.
 * A role switch disconnects BLE. A scan remains active for 15 seconds. */
int fm1_ble_set_role(enum fm1_ble_role role);
int fm1_ble_scan(void);
int fm1_ble_connect(unsigned index);
int fm1_ble_disconnect(void);
int fm1_ble_forget_bonds(void);
void fm1_ble_get_status(struct fm1_ble_status *status);
int fm1_ble_get_device(unsigned index, struct fm1_ble_device *device);

/* Optional control-task hook for complete SysEx frames (including F0/F7),
 * at most 1024 bytes. Copy or queue what is needed; the transport owns the
 * buffer after return. Firmware-update frames are handled before this hook.
 * The default ignores application SysEx. */
void fm1_app_sysex(const uint8_t *message, size_t bytes);

/* --- Audio timing ------------------------------------------------------- */

struct fm1_audio_stats {
    uint32_t blocks, short_intervals, overruns, peak_ticks;
};
void fm1_audio_stats_read(struct fm1_audio_stats *stats);

/* --- Panel -------------------------------------------------------------- */

#define FM1_SCREEN_WIDTH 240U
#define FM1_SCREEN_HEIGHT 240U

struct fm1_controls {
    uint32_t keys;      /* bits 0..26: F3..G5 */
    uint32_t buttons;   /* physical panel buttons 0..13 */
    int16_t encoders[7];/* signed detents since the previous read */
    uint16_t knob, battery;
};

void fm1_controls_init(void);
void fm1_controls_read(struct fm1_controls *state);
void fm1_led_set(unsigned key, int on); /* 0..13 buttons, 14..40 note keys */

/* Wrapping control millisecond clock; measure with unsigned subtraction. */
uint32_t fm1_millis(void);

/* Ask the control task to run deferred work after queuing a message. */
void fm1_control_request(void);

/* --- Screen ------------------------------------------------------------- */

void fm1_screen_init(void);
void fm1_screen_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t rgb565);

/* Native-endian RGB565. The call completes before return, so the caller may
 * reuse the pixels. The rectangle must fit inside the panel. Returns 0 or a
 * negative error. */
int fm1_screen_blit(unsigned x, unsigned y, unsigned w, unsigned h,
                    const uint16_t *pixels, size_t count);

#define FM1_SCREEN_DMA_PIXELS (240U * 16U)

/* Non-blocking band transfer of at most FM1_SCREEN_DMA_PIXELS. On success (0)
 * the buffer is byte-swapped in place and borrowed until pending returns 0;
 * do not touch it meanwhile. 1 means busy, negative means bad input or error. */
int fm1_screen_submit(unsigned x, unsigned y, unsigned w, unsigned h,
                      uint16_t *pixels, size_t count);

/* 1 while a transfer runs, 0 idle, negative on timeout (transfer stopped). */
int fm1_screen_pending(void);

/* --- Persistent user Flash ---------------------------------------------- */

#define FM1_FLASH_SECTOR_BYTES 4096U

enum fm1_flash_result {
    FM1_FLASH_OK = 0,
    FM1_FLASH_INVALID = -1,
    FM1_FLASH_IO = -2,
    FM1_FLASH_BUSY = -3,
    FM1_FLASH_NEEDS_ERASE = -4,
    FM1_FLASH_CONTEXT = -5,
    FM1_FLASH_UNAVAILABLE = -6
};

/* Offsets start at zero in this firmware's user partition. Call these only
 * from the control task with interrupts enabled, never from audio, timer,
 * MIDI or SysEx callbacks. Writes and erases briefly silence audio. There is
 * no allocation and no automatic erase; erase clears exactly one aligned
 * sector. Write verifies 1->0 transitions and readback. The API is not
 * transactional: interrupted writes may be partial, so use versioned/CRC
 * records and two sectors when data must survive. */
uint32_t fm1_flash_size(void);
int fm1_flash_read(uint32_t offset, void *data, size_t bytes);
int fm1_flash_write(uint32_t offset, const void *data, size_t bytes);
int fm1_flash_erase(uint32_t offset);

#endif
