#include "nano1g/firmware.h"

#include "nano1g/ram.h"
#include "nano1g/state.h"
#include "nano1g/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

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

static bool fourcc_readable(const uint8_t *p, const char *name) {
    return p[7] == (uint8_t)name[0] && p[6] == (uint8_t)name[1] &&
           p[5] == (uint8_t)name[2] && p[4] == (uint8_t)name[3];
}

static bool load_wrapped_firmware(n1g_state_t *s, const uint8_t *fw, size_t fw_size) {
    if (fw_size < 0x4200 || rd32(fw + 0x100) != 0x5b68695du) {
        return false;
    }
    uint32_t dir_off = rd32(fw + 0x104);
    uint16_t fmt = rd16(fw + 0x10a);
    size_t d = (size_t)dir_off + 0x200u;
    if (d + 40 > fw_size) {
        return false;
    }

    while (d + 40 <= fw_size) {
        if (rd32(fw + d) == 0) {
            break;
        }
        if (fourcc_readable(fw + d, "osos")) {
            uint32_t dev_off = rd32(fw + d + 12);
            uint32_t length = rd32(fw + d + 16);
            uint32_t addr = rd32(fw + d + 20);
            uint32_t entry = rd32(fw + d + 24);
            size_t payload_off = (size_t)dev_off + 0x200u;
            if (payload_off + length > fw_size) {
                n1g_info(s, "wrapped firmware osos out of range payload=0x%zx len=%u size=%zu",
                         payload_off, length, fw_size);
                return false;
            }
            uint8_t *dst = n1g_ram_ptr(s, addr, length);
            if (!dst) {
                n1g_info(s, "wrapped firmware osos does not fit addr=0x%08x len=%u", addr, length);
                return false;
            }
            memcpy(dst, fw + payload_off, length);
            s->opts.load_addr = addr;
            if (!s->opts.entry_set) {
                s->opts.entry = addr + entry;
                s->opts.entry_set = true;
            }
            n1g_info(s, "loaded wrapped osos fmt=%u file_off=0x%zx addr=0x%08x entry=0x%08x len=%u",
                     fmt, payload_off, addr, addr + entry, length);
            return true;
        }
        d += 40;
    }
    return false;
}

static bool load_ipod_firmware(n1g_state_t *s, const uint8_t *fw, size_t fw_size) {
    if (fw_size <= 8u || memcmp(fw + 4, "nano", 4) != 0) {
        return false;
    }

    size_t payload_size = fw_size - 8u;
    uint8_t *dst = n1g_ram_ptr(s, s->opts.load_addr, payload_size);
    if (!dst) {
        n1g_info(s, "ipod firmware does not fit at load address 0x%08x size=%zu",
                 s->opts.load_addr,
                 payload_size);
        return false;
    }

    memcpy(dst, fw + 8, payload_size);
    if (!s->opts.entry_set) {
        s->opts.entry = s->opts.load_addr;
        s->opts.entry_set = true;
    }
    n1g_info(s,
             "loaded ipod firmware model=nano file_off=0x8 addr=0x%08x entry=0x%08x len=%zu",
             s->opts.load_addr,
             s->opts.entry,
             payload_size);
    return true;
}

static bool looks_like_wrapped_firmware(const uint8_t *fw, size_t fw_size) {
    return fw_size >= 0x4200u && rd32(fw + 0x100) == 0x5b68695du;
}

static bool looks_like_zip(const uint8_t *fw, size_t fw_size) {
    return fw_size >= 4u && fw[0] == 'P' && fw[1] == 'K' &&
           fw[2] == 0x03u && fw[3] == 0x04u;
}

bool n1g_load_firmware_from_disk(n1g_state_t *s) {
    if (!s->disk.data || s->disk.size < 0x1200) {
        n1g_info(s, "no disk image loaded for firmware-from-disk");
        return false;
    }

    const uint8_t magic[4] = {0x5d, 0x69, 0x68, 0x5b};
    for (size_t pos = 0x100; pos + 4 <= s->disk.size; pos += 4) {
        if (memcmp(s->disk.data + pos, magic, sizeof(magic)) != 0) {
            continue;
        }
        size_t start = pos - 0x100u;
        uint32_t dir_off = rd32(s->disk.data + pos + 4);
        if (dir_off > 0x100000u || start + (size_t)dir_off + 0x228u > s->disk.size) {
            continue;
        }
        size_t entries = start + (size_t)dir_off + 0x200u;
        if (!fourcc_readable(s->disk.data + entries, "osos")) {
            continue;
        }
        if (!load_wrapped_firmware(s, s->disk.data + start, s->disk.size - start)) {
            continue;
        }
        n1g_info(s, "loaded disk wrapped firmware image_start=0x%zx", start);
        return true;
    }

    n1g_info(s, "no wrapped firmware image found in disk");
    return false;
}

bool n1g_load_firmware(n1g_state_t *s) {
    uint8_t *fw = NULL;
    size_t fw_size = 0;
    if (!n1g_read_file(s->opts.firmware_path, &fw, &fw_size)) {
        n1g_info(s, "failed to read firmware: %s", s->opts.firmware_path);
        return false;
    }
    if (load_wrapped_firmware(s, fw, fw_size)) {
        free(fw);
        return true;
    }
    if (load_ipod_firmware(s, fw, fw_size)) {
        free(fw);
        return true;
    }
    uint8_t *dst = n1g_ram_ptr(s, s->opts.load_addr, fw_size);
    if (!dst) {
        n1g_info(s, "firmware does not fit at load address 0x%08x size=%zu", s->opts.load_addr, fw_size);
        free(fw);
        return false;
    }
    memcpy(dst, fw, fw_size);
    n1g_info(s, "loaded firmware %s size=%zu at 0x%08x", s->opts.firmware_path, fw_size, s->opts.load_addr);
    free(fw);
    return true;
}

bool n1g_load_flash_rom(n1g_state_t *s) {
    if (!s->opts.flash_path) {
        return true;
    }

    uint8_t *rom = NULL;
    size_t rom_size = 0;
    if (!n1g_read_file(s->opts.flash_path, &rom, &rom_size)) {
        n1g_info(s, "failed to read flash ROM: %s", s->opts.flash_path);
        if (s->opts.profile == N1G_PROFILE_APPLE && s->opts.boot_mode == N1G_BOOT_FLASH) {
            n1g_info(s,
                     "Apple official boot needs a raw %u-byte Nano 1G boot ROM/NOR dump; pass --flash-rom or set NANO1G_APPLE_BOOTROM",
                     N1G_FLASH_SIZE);
        }
        return false;
    }
    if (s->opts.profile == N1G_PROFILE_APPLE && s->opts.boot_mode == N1G_BOOT_FLASH &&
        rom_size != N1G_FLASH_SIZE) {
        n1g_info(s,
                 "Apple official boot requires a raw %u-byte Nano 1G boot ROM/NOR dump at --flash-rom; got %zu bytes from %s",
                 N1G_FLASH_SIZE,
                 rom_size,
                 s->opts.flash_path);
        if (looks_like_wrapped_firmware(rom, rom_size)) {
            n1g_info(s, "flash source looks like a wrapped firmware bundle, not reset-vector boot ROM");
        } else if (looks_like_zip(rom, rom_size)) {
            n1g_info(s, "flash source looks like a ZIP updater/container, not reset-vector boot ROM");
        }
        free(rom);
        return false;
    }
    if (rom_size > N1G_FLASH_SIZE) {
        n1g_info(s, "flash ROM too large: %s size=%zu max=%u",
                 s->opts.flash_path,
                 rom_size,
                 N1G_FLASH_SIZE);
        free(rom);
        return false;
    }

    memset(s->flash.bytes, 0xff, sizeof(s->flash.bytes));
    memcpy(s->flash.bytes, rom, rom_size);
    n1g_info(s, "loaded flash ROM %s size=%zu at 0x%08x",
             s->opts.flash_path,
             rom_size,
             N1G_FLASH_BASE);
    free(rom);
    return true;
}
