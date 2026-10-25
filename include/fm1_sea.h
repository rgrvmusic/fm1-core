/* SPDX-License-Identifier: MIT */
/* Fixed SEA5/SEA6 decoder supplied by the FM-1 runtime. */
#ifndef FM1_SEA_H
#define FM1_SEA_H

#include <stddef.h>
#include <stdint.h>

/* 320 mono frames per chunk, 20 frames per scale group. The decoder uses no
 * OS calls, allocation or shared mutable state. */
#define FM1_SEA_FRAMES 320u
#define FM1_SEA_GROUP 20u

/* Decode one complete chunk. DQT holds 16 * (1 << bits) int16 entries.
 * Returns 0 on success or -1 for an invalid argument/header/size; a failure
 * leaves the output unchanged. */
int fm1_sea_decode(const uint8_t *chunk, size_t bytes, unsigned bits,
                   const int16_t *dqt, int16_t pcm[FM1_SEA_FRAMES]);

/* Decode groups first..last (0..15) and emit the final 20 PCM frames.
 * first == 0 loads the seed; otherwise state must describe group first-1 of
 * the same chunk. Returns 0 on success, -1 on failure without changing
 * state or output. */
int fm1_sea_decode_groups(const uint8_t *chunk, size_t bytes, unsigned bits,
                          const int16_t *dqt, unsigned first, unsigned last,
                          int32_t state[8], int16_t pcm[FM1_SEA_GROUP]);

#endif
