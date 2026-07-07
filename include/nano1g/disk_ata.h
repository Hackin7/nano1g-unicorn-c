#ifndef NANO1G_DISK_ATA_H
#define NANO1G_DISK_ATA_H

#include "nano1g/state.h"

bool n1g_disk_load(n1g_state_t *s, const char *path);
void n1g_disk_destroy(n1g_state_t *s);
uint32_t n1g_disk_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_disk_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);

#endif
