#include "nano1g/firmware.h"

#include "nano1g/ram.h"
#include "nano1g/state.h"
#include "nano1g/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool n1g_read_file(const char *path, uint8_t **out, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long len = ftell(f);
    if (len < 0) {
        fclose(f);
        return false;
    }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (!buf) {
        fclose(f);
        return false;
    }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return false;
    }
    fclose(f);
    *out = buf;
    *out_size = (size_t)len;
    return true;
}

bool n1g_load_firmware(n1g_state_t *s) {
    uint8_t *fw = NULL;
    size_t fw_size = 0;
    if (!n1g_read_file(s->opts.firmware_path, &fw, &fw_size)) {
        n1g_log(s, "failed to read firmware: %s", s->opts.firmware_path);
        return false;
    }
    uint8_t *dst = n1g_ram_ptr(s, s->opts.load_addr, fw_size);
    if (!dst) {
        n1g_log(s, "firmware does not fit at load address 0x%08x size=%zu", s->opts.load_addr, fw_size);
        free(fw);
        return false;
    }
    memcpy(dst, fw, fw_size);
    n1g_log(s, "loaded firmware %s size=%zu at 0x%08x", s->opts.firmware_path, fw_size, s->opts.load_addr);
    free(fw);
    return true;
}
