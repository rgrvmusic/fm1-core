/* SPDX-License-Identifier: MIT */
/* External audio streams supplied by the fixed FM-1 runtime. */
#ifndef FM1_AUDIO_IO_H
#define FM1_AUDIO_IO_H

#include "fm1.h"

/* Audio-interrupt context, once per FM1_BLOCK_FRAMES block. Each call copies
 * samples and never retains a caller buffer, waits or allocates.
 *
 * fm1_usb_audio_read returns FM1_BLOCK_FRAMES on a full block, 0 while the
 * stream is inactive or starved, -1 for an invalid pointer/count, and -2 when
 * the host changed the stream during the copy. Samples are signed Q23.
 * The gain array receives host left/right volume, 128 = unity.
 *
 * fm1_bluetooth_audio_read returns the same frame count with signed 16-bit
 * samples. Mixing is the application's responsibility. */
FM1_AUDIO_CODE int fm1_usb_audio_read(int32_t *pcm, unsigned frames, uint16_t gains_q7[2]);
FM1_AUDIO_CODE int fm1_bluetooth_audio_read(int16_t *pcm, unsigned frames);

/* Queue final signed Q23 output for USB capture. Returns the queued frame
 * count (possibly partial), 0 while inactive or full, or a negative error.
 * The caller's buffer is unchanged. */
FM1_AUDIO_CODE int fm1_usb_audio_write(const int32_t *pcm, unsigned frames);

#endif
