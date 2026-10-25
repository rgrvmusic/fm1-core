/* SPDX-License-Identifier: MIT */
/* Weak defaults for the application contract.
 *
 * These keep the platform linkable on its own. An application replaces any
 * hook by defining it with strong linkage.
 */
#include <string.h>

#include "fm1_app.h"
#include "fm1_device.h"
#include "fm1_engine_control.h"

#define WEAK __attribute__((weak))

/* The runtime never inspects the project contents; it only stores and passes
 * the pointer between application hooks. */
static uint32_t scratch_project[FM1_PROJECT_BYTES / 4U] __attribute__((aligned(8)));

WEAK int fm1_app_render(int32_t *output, unsigned frames)
{
    return fm1_engine_render(output, frames);
}

WEAK void groove_screen_init(void)
{
    fm1_controls_init();
    fm1_screen_init();
}

WEAK void groove_screen_control(void)
{
    /* A custom interface drives the panel from here. */
}

WEAK void groove_fm_library_init(void) {}
WEAK void groove_midi_init(void) {}
WEAK void groove_midi_poll(void) {}
WEAK void groove_midi_panic(void) {}
WEAK void groove_midi_clock_reset(void) {}
WEAK void groove_midi_track_reset(unsigned track) { (void)track; }
WEAK void groove_midi_transport(unsigned owner, uint8_t status)
{
    (void)owner;
    (void)status;
}
WEAK void groove_midi_clock(unsigned owner, unsigned tempo)
{
    (void)owner;
    (void)tempo;
}
WEAK void groove_midi_note(unsigned track, unsigned note, unsigned velocity)
{
    (void)track;
    (void)note;
    (void)velocity;
}

WEAK unsigned groove_midi_mode(unsigned track)
{
    (void)track;
    return 0U; /* local playback */
}

/* Zero is a valid, silent patch. Applications load their own voice here. */
WEAK void groove_fm_program(uint8_t patch[156], unsigned program)
{
    (void)program;
    memset(patch, 0, 156U);
}

WEAK void groove_project_init(void) {}
WEAK void groove_project_poll(void) {}
WEAK void groove_project_metadata(const struct groove_project *project)
{
    (void)project;
}

WEAK void groove_project_name(char name[13])
{
    name[0] = 0;
}

WEAK struct groove_project *groove_project_scratch(void)
{
    return (struct groove_project *)(void *)scratch_project;
}

WEAK void groove_samples_init(void) {}
WEAK void groove_samples_poll(void) {}
WEAK void groove_transfer_init(void) {}
WEAK void groove_transfer_poll(void) {}
