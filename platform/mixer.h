/* SPDX-License-Identifier: MIT */
/* Final stage of the output block: master gain and external sources. */
#ifndef FM1_PLATFORM_MIXER_H
#define FM1_PLATFORM_MIXER_H

#include "fm1.h"

/* Reset before audio starts, and from the audio interrupt exclusion window. */
void fm1_output_reset(void);

/* Apply master gain, USB audio and Bluetooth audio, then record the result.
 * Audio interrupt only; output holds FM1_BLOCK_WORDS Q23 samples. */
FM1_AUDIO_CODE void fm1_output_mix(int32_t *output);

#endif
