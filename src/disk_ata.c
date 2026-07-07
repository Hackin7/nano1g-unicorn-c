#include "nano1g/disk_ata.h"

#include "nano1g/firmware.h"
#include "nano1g/trace.h"

#include <stdlib.h>

bool n1g_disk_load(n1g_state_t *s, const char *path) {
    if (!path) {
        return true;
    }
    if (!n1g_read_file(path, &s->disk.data, &s->disk.size)) {
        n1g_log(s, "failed to read disk image: %s", path);
        return false;
    }
    s->disk.status = 0x40;
    n1g_log(s, "loaded disk %s size=%zu", path, s->disk.size);
    return true;
}

void n1g_disk_destroy(n1g_state_t *s) {
    free(s->disk.data);
    s->disk.data = NULL;
    s->disk.size = 0;
}

static uint16_t disk_word(n1g_state_t *s) {
    size_t off = (size_t)s->disk.selected_lba * 512u + s->disk.data_index;
    s->disk.data_index += 2;
    if (!s->disk.data || off + 1 >= s->disk.size) {
        return 0xffff;
    }
    s->counters.disk_reads++;
    return (uint16_t)(s->disk.data[off] | ((uint16_t)s->disk.data[off + 1] << 8));
}

uint32_t n1g_disk_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    (void)size;
    switch (offset) {
    case 0x00:
        return disk_word(s);
    case 0x1c:
        return s->disk.status;
    default:
        return 0;
    }
}

void n1g_disk_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    (void)size;
    switch (offset) {
    case 0x00:
        break;
    case 0x08:
        s->disk.sector_count = (uint8_t)value;
        break;
    case 0x0c:
        s->disk.selected_lba = (s->disk.selected_lba & 0xffffff00u) | (value & 0xffu);
        break;
    case 0x10:
        s->disk.selected_lba = (s->disk.selected_lba & 0xffff00ffu) | ((value & 0xffu) << 8);
        break;
    case 0x14:
        s->disk.selected_lba = (s->disk.selected_lba & 0xff00ffffu) | ((value & 0xffu) << 16);
        break;
    case 0x18:
        s->disk.selected_lba = (s->disk.selected_lba & 0x00ffffffu) | ((value & 0x0fu) << 24);
        break;
    case 0x1c:
        if ((value & 0xffu) == 0x20u || (value & 0xffu) == 0x24u) {
            s->disk.status = 0x58;
            s->disk.data_index = 0;
        }
        break;
    default:
        break;
    }
}
