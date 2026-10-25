/* SPDX-License-Identifier: MIT */
/* Firmware layout and build identity expected by the bootloader. */
#ifndef FM1_LAYOUT_H
#define FM1_LAYOUT_H

#include <stdint.h>

struct fm1_firmware_layout {
    uint32_t user_bytes;
    uint32_t update_family;
};

extern const struct fm1_firmware_layout fm1_layout;
extern const unsigned char fm1_build_id[32];
extern const unsigned char fm1_update_identity[27];

#endif
