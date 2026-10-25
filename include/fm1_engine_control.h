/* SPDX-License-Identifier: MIT */
/* Control surface of the fixed FM-1 instrument engine. */
#ifndef FM1_ENGINE_CONTROL_H
#define FM1_ENGINE_CONTROL_H

#include "fm1.h"

/* Audio-interrupt context, once per block. Fill FM1_BLOCK_FRAMES stereo
 * frames with signed Q23 output. Master gain, external mixing and recording
 * belong to the application. frames must equal FM1_BLOCK_FRAMES and output
 * must hold FM1_BLOCK_WORDS words. Returns 0, or -1 for invalid arguments. */
FM1_AUDIO_CODE int fm1_engine_render(int32_t *output, unsigned frames);

/* Control-task edit transactions. Pause/resume nest. */
void fm1_engine_pause(void);
void fm1_engine_resume(void);
void fm1_engine_stop(void);

/* Current master output level, 0..127. */
FM1_AUDIO_CODE unsigned fm1_engine_output_level(void);

struct fm1_engine_stats {
    uint32_t submitted, completed, underruns, midi_dropped;
};
void fm1_engine_stats_read(struct fm1_engine_stats *output);

#endif
