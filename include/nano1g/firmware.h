#ifndef NANO1G_FIRMWARE_H
#define NANO1G_FIRMWARE_H

#include <stddef.h>
#include <stdint.h>

#include "nano1g/state.h"

bool n1g_read_file(const char *path, uint8_t **out, size_t *out_size);
bool n1g_load_firmware(n1g_state_t *s);

#endif
