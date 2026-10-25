/* SPDX-License-Identifier: MIT */
/* Application hooks expected by the FM-1 binary SDK.
 *
 * The platform supplies weak fallbacks for every hook below, so a minimal
 * firmware links without an application. Define any of them strongly to take
 * over that part of the instrument. Do not call these from the audio
 * interrupt unless the documentation of the specific hook allows it.
 */
#ifndef FM1_APP_H
#define FM1_APP_H

#include "fm1.h"

/* The project model belongs to the application; the SDK only passes it
 * through. 4064 bytes is the size reserved by the fixed runtime. */
struct groove_project;
#define FM1_PROJECT_BYTES 4064U

/* Replace the whole per-block renderer. The default calls
 * fm1_engine_render(). Called from the audio interrupt with output holding
 * FM1_BLOCK_WORDS signed Q23 words. Return 0 on success. */
int fm1_app_render(int32_t *output, unsigned frames);

/* Display and input. fm1_screen_init() also starts the panel scanner. */
void groove_screen_init(void);
void groove_screen_control(void);

/* Sound library and patch loading. */
void groove_fm_library_init(void);
void groove_fm_program(uint8_t patch[156], unsigned program);

/* MIDI routing. groove_midi_mode() returns 0 for a track that should stay
 * local and nonzero when events are routed away. */
void groove_midi_init(void);
void groove_midi_poll(void);
void groove_midi_panic(void);
unsigned groove_midi_mode(unsigned track);
void groove_midi_note(unsigned track, unsigned note, unsigned velocity);
void groove_midi_track_reset(unsigned track);
void groove_midi_transport(unsigned owner, uint8_t status);
void groove_midi_clock(unsigned owner, unsigned tempo);
void groove_midi_clock_reset(void);

/* Project storage and naming. */
void groove_project_init(void);
void groove_project_poll(void);
void groove_project_metadata(const struct groove_project *project);
void groove_project_name(char name[13]);
struct groove_project *groove_project_scratch(void);

/* Sample slots. */
void groove_samples_init(void);
void groove_samples_poll(void);

/* Firmware transfer / SysEx bulk handler. */
void groove_transfer_init(void);
void groove_transfer_poll(void);

#endif
