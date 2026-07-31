#include "nano1g/disk_ata.h"

#include "nano1g/firmware.h"
#include "nano1g/ram.h"
#include "nano1g/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ATA_ERR 0x01u
#define ATA_DRQ 0x08u
#define ATA_DSC 0x10u
#define ATA_DF 0x20u
#define ATA_DRDY 0x40u
#define ATA_BSY 0x80u

#define ATA_CMD_READ_SECTORS 0x20u
#define ATA_CMD_READ_SECTORS_EXT 0x24u
#define ATA_CMD_WRITE_SECTORS 0x30u
#define ATA_CMD_WRITE_SECTORS_EXT 0x34u
#define ATA_CMD_READ_MULTIPLE 0xc4u
#define ATA_CMD_WRITE_MULTIPLE 0xc5u
#define ATA_CMD_SET_MULTIPLE_MODE 0xc6u
#define ATA_CMD_READ_DMA 0xc8u
#define ATA_CMD_READ_DMA_NO_RETRY 0xc9u
#define ATA_CMD_WRITE_DMA 0xcau
#define ATA_CMD_WRITE_DMA_NO_RETRY 0xcbu
#define ATA_CMD_STANDBY_IMMEDIATE 0xe0u
#define ATA_CMD_FLUSH_CACHE 0xe7u
#define ATA_CMD_SET_FEATURES 0xefu
#define ATA_CMD_IDENTIFY_DEVICE 0xecu

#define PP_ATA_DATA 0x1e0u
#define PP_ATA_ERROR_FEATURES 0x1e4u
#define PP_ATA_SECTOR_COUNT 0x1e8u
#define PP_ATA_LBA_LOW 0x1ecu
#define PP_ATA_LBA_MID 0x1f0u
#define PP_ATA_LBA_HIGH 0x1f4u
#define PP_ATA_DEVICE 0x1f8u
#define PP_ATA_COMMAND_STATUS 0x1fcu
#define PP_IDE0_CFG 0x28u
#define PP_IDE1_CFG 0x2cu
#define PP_ATA_ALT_STATUS 0x3f8u

#define PP_IDE_DMA_CONTROL 0x400u
#define PP_IDE_DMA_STATUS 0x404u
#define PP_IDE_DMA_LENGTH 0x408u
#define PP_IDE_DMA_ADDR 0x40cu
#define PP_IDE_DMA_UNKNOWN 0x410u
#define PP5020_IDE_IRQ_BIT (1u << 23)
#define ATA_DEVCTL_NIEN 0x02u

typedef enum n1g_ata_transfer {
    N1G_ATA_TRANSFER_NONE = 0,
    N1G_ATA_TRANSFER_IDENTIFY = 1,
    N1G_ATA_TRANSFER_READ = 2,
    N1G_ATA_TRANSFER_WRITE = 3,
    N1G_ATA_TRANSFER_DMA_READ = 4,
    N1G_ATA_TRANSFER_DMA_WRITE = 5
} n1g_ata_transfer_t;

static void set_word(uint8_t *buf, uint32_t index, uint16_t value) {
    buf[index * 2u] = (uint8_t)(value & 0xffu);
    buf[index * 2u + 1u] = (uint8_t)(value >> 8);
}

static uint16_t get_word(const uint8_t *buf, uint32_t index) {
    return (uint16_t)(buf[index * 2u] | ((uint16_t)buf[index * 2u + 1u] << 8));
}

static void set_ide_irq(n1g_state_t *s, bool pending) {
    s->disk.irq_pending = pending;
    if (pending && (s->disk.device_control & ATA_DEVCTL_NIEN) == 0) {
        s->intc.cpu_status |= PP5020_IDE_IRQ_BIT;
        s->intc.cop_status |= PP5020_IDE_IRQ_BIT;
    } else {
        s->intc.cpu_status &= ~PP5020_IDE_IRQ_BIT;
        s->intc.cop_status &= ~PP5020_IDE_IRQ_BIT;
    }
}

static void set_ata_string(uint8_t *buf, uint32_t word_index, uint32_t word_count, const char *text) {
    uint32_t byte_count = word_count * 2u;
    for (uint32_t i = 0; i < byte_count; i++) {
        buf[word_index * 2u + i] = ' ';
    }
    size_t len = strlen(text);
    if (len > byte_count) {
        len = byte_count;
    }
    for (size_t i = 0; i < len; i++) {
        uint32_t dst = word_index * 2u + (uint32_t)i;
        buf[dst ^ 1u] = (uint8_t)text[i];
    }
}

static void build_identify(n1g_state_t *s) {
    memset(s->disk.identify, 0, sizeof(s->disk.identify));
    uint32_t sectors = (uint32_t)(s->disk.size / 512u);
    if (sectors == 0) {
        sectors = 1;
    }
    if (sectors > 0x0fffffffu) {
        sectors = 0x0fffffffu;
    }

    set_word(s->disk.identify, 0, 0x0040u);  /* Fixed disk. */
    set_word(s->disk.identify, 1, 16383u);
    set_word(s->disk.identify, 3, 16u);
    set_word(s->disk.identify, 4, 63u * 512u);
    set_word(s->disk.identify, 5, 512u);
    set_word(s->disk.identify, 6, 63u);
    set_ata_string(s->disk.identify, 10, 10, "NANO1G0001");
    set_ata_string(s->disk.identify, 23, 4, "0.1");
    set_ata_string(s->disk.identify, 27, 20, "NANO1G UNICORN DISK");
    set_word(s->disk.identify, 47, 0x8001u);
    set_word(s->disk.identify, 49, 0x0700u); /* DMA, LBA, and IORDY supported. */
    set_word(s->disk.identify, 51, 0x0200u);
    set_word(s->disk.identify, 52, 0x0200u);
    set_word(s->disk.identify, 53, 0x0003u);
    set_word(s->disk.identify, 54, 16383u);
    set_word(s->disk.identify, 55, 16u);
    set_word(s->disk.identify, 56, 63u);
    set_word(s->disk.identify, 57, (uint16_t)(sectors & 0xffffu));
    set_word(s->disk.identify, 58, (uint16_t)(sectors >> 16));
    set_word(s->disk.identify, 59, 0x0101u);
    set_word(s->disk.identify, 60, (uint16_t)(sectors & 0xffffu));
    set_word(s->disk.identify, 61, (uint16_t)(sectors >> 16));
    set_word(s->disk.identify, 63, 0x0007u);
    set_word(s->disk.identify, 64, 0x0007u);
    set_word(s->disk.identify, 65, 120u);
    set_word(s->disk.identify, 66, 120u);
    set_word(s->disk.identify, 67, 120u);
    set_word(s->disk.identify, 68, 120u);
    set_word(s->disk.identify, 80, 0x001eu);
    set_word(s->disk.identify, 83, 0x4000u);
}

static void finish_transfer(n1g_state_t *s) {
    s->disk.status = ATA_DRDY | ATA_DSC;
    s->disk.transfer_kind = N1G_ATA_TRANSFER_NONE;
    s->disk.data_index = 0;
    s->disk.sectors_remaining = 0;
    set_ide_irq(s, true);
}

bool n1g_disk_load(n1g_state_t *s, const char *path) {
    if (!path) {
        build_identify(s);
        s->disk.status = ATA_DRDY;
        s->disk.ide0_cfg = 0;
        s->disk.ide1_cfg = 0;
        return true;
    }
    if (!n1g_read_file(path, &s->disk.data, &s->disk.size)) {
        n1g_info(s, "failed to read disk image: %s", path);
        return false;
    }
    build_identify(s);
    s->disk.status = ATA_DRDY;
    s->disk.ide0_cfg = 0;
    s->disk.ide1_cfg = 0;
    n1g_info(s, "loaded disk %s size=%zu", path, s->disk.size);
    return true;
}

bool n1g_disk_save(n1g_state_t *s, const char *path) {
    if (!path || !s->disk.data || s->disk.size == 0) {
        return false;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    bool ok = fwrite(s->disk.data, 1, s->disk.size, f) == s->disk.size;
    if (fclose(f) != 0) {
        ok = false;
    }
    if (ok) {
        n1g_info(s, "saved disk %s size=%zu", path, s->disk.size);
    }
    return ok;
}

void n1g_disk_destroy(n1g_state_t *s) {
    free(s->disk.data);
    s->disk.data = NULL;
    s->disk.size = 0;
}

static uint16_t read_identify_word(n1g_state_t *s) {
    uint16_t out = get_word(s->disk.identify, s->disk.data_index / 2u);
    s->disk.data_index += 2u;
    if (s->disk.data_index >= sizeof(s->disk.identify)) {
        finish_transfer(s);
    }
    return out;
}

static uint16_t read_sector_word(n1g_state_t *s) {
    size_t off = (size_t)s->disk.transfer_lba * 512u + s->disk.data_index;
    uint16_t out = 0xffffu;
    if (s->disk.data && off + 1u < s->disk.size) {
        out = (uint16_t)(s->disk.data[off] | ((uint16_t)s->disk.data[off + 1u] << 8));
    }
    s->disk.data_index += 2;
    s->counters.disk_reads++;
    if (s->disk.data_index >= 512u) {
        s->disk.data_index = 0;
        s->disk.transfer_lba++;
        if (s->disk.sectors_remaining > 0) {
            s->disk.sectors_remaining--;
        }
        if (s->disk.sectors_remaining == 0) {
            finish_transfer(s);
        } else {
            set_ide_irq(s, true);
        }
    }
    return out;
}

static void write_sector_word(n1g_state_t *s, uint16_t value) {
    size_t off = (size_t)s->disk.transfer_lba * 512u + s->disk.data_index;
    if (s->disk.data && off + 1u < s->disk.size) {
        s->disk.data[off] = (uint8_t)(value & 0xffu);
        s->disk.data[off + 1u] = (uint8_t)(value >> 8);
        s->counters.disk_writes++;
    }
    s->disk.data_index += 2u;
    if (s->disk.data_index >= 512u) {
        s->disk.data_index = 0;
        s->disk.transfer_lba++;
        if (s->disk.sectors_remaining > 0) {
            s->disk.sectors_remaining--;
        }
        if (s->disk.sectors_remaining == 0) {
            finish_transfer(s);
        } else {
            set_ide_irq(s, true);
        }
    }
}

static uint16_t peek_sector_word(const n1g_state_t *s) {
    size_t off = (size_t)s->disk.selected_lba * 512u;
    if (s->disk.data && off + 1u < s->disk.size) {
        return (uint16_t)(s->disk.data[off] | ((uint16_t)s->disk.data[off + 1u] << 8));
    }
    return 0xffffu;
}

static uint16_t disk_word(n1g_state_t *s) {
    if ((s->disk.status & ATA_DRQ) == 0) {
        return 0xffffu;
    }
    if (s->disk.transfer_kind == N1G_ATA_TRANSFER_IDENTIFY) {
        return read_identify_word(s);
    }
    if (s->disk.transfer_kind == N1G_ATA_TRANSFER_READ) {
        return read_sector_word(s);
    }
    return 0xffffu;
}

static uint32_t data_read(n1g_state_t *s, uint32_t size) {
    uint32_t lo = disk_word(s);
    if (size == 4) {
        return lo | ((uint32_t)disk_word(s) << 16);
    }
    return lo;
}

static void data_write(n1g_state_t *s, uint32_t size, uint32_t value) {
    if ((s->disk.status & ATA_DRQ) == 0 || s->disk.transfer_kind != N1G_ATA_TRANSFER_WRITE) {
        return;
    }
    write_sector_word(s, (uint16_t)(value & 0xffffu));
    if (size == 4 && (s->disk.status & ATA_DRQ) != 0) {
        write_sector_word(s, (uint16_t)(value >> 16));
    }
}

static void start_identify(n1g_state_t *s) {
    build_identify(s);
    s->disk.error = 0;
    s->disk.data_index = 0;
    s->disk.sectors_remaining = 1;
    s->disk.transfer_kind = N1G_ATA_TRANSFER_IDENTIFY;
    s->disk.status = ATA_DRDY | ATA_DSC | ATA_DRQ;
    set_ide_irq(s, true);
    if (s->opts.verbose) {
        n1g_info(s,
                 "ata identify sectors=%u word5_sector_bytes=%u status=0x%02x",
                 (unsigned)(s->disk.size / 512u),
                 (unsigned)get_word(s->disk.identify, 5),
                 s->disk.status);
    }
}

static void start_read(n1g_state_t *s) {
    uint16_t count = s->disk.sector_count == 0 ? 256u : s->disk.sector_count;
    s->disk.error = 0;
    s->disk.transfer_lba = s->disk.selected_lba;
    s->disk.sectors_remaining = count;
    s->disk.data_index = 0;
    s->disk.transfer_kind = N1G_ATA_TRANSFER_READ;
    s->disk.status = ATA_DRDY | ATA_DSC | ATA_DRQ;
    set_ide_irq(s, true);
    if (s->opts.verbose) {
        n1g_info(s,
                 "ata read cmd=0x%02x lba=%u count=%u first_word=0x%04x status=0x%02x",
                 s->disk.command,
                 s->disk.transfer_lba,
                 s->disk.sectors_remaining,
                 (unsigned)peek_sector_word(s),
                 s->disk.status);
    }
}

static void start_write(n1g_state_t *s) {
    uint16_t count = s->disk.sector_count == 0 ? 256u : s->disk.sector_count;
    s->disk.error = 0;
    s->disk.transfer_lba = s->disk.selected_lba;
    s->disk.sectors_remaining = count;
    s->disk.data_index = 0;
    s->disk.transfer_kind = N1G_ATA_TRANSFER_WRITE;
    s->disk.status = ATA_DRDY | ATA_DSC | ATA_DRQ;
    set_ide_irq(s, true);
    if (s->opts.verbose) {
        n1g_info(s,
                 "ata write cmd=0x%02x lba=%u count=%u status=0x%02x",
                 s->disk.command,
                 s->disk.transfer_lba,
                 s->disk.sectors_remaining,
                 s->disk.status);
    }
}


static void schedule_dma(n1g_state_t *s) {
    s->disk.dma_pending =
        (s->disk.dma_control & 1u) != 0 &&
        (s->disk.transfer_kind == N1G_ATA_TRANSFER_DMA_READ ||
         s->disk.transfer_kind == N1G_ATA_TRANSFER_DMA_WRITE);
}

static void complete_dma_transfer(n1g_state_t *s) {
    if ((s->disk.dma_control & 1u) == 0 ||
        (s->disk.transfer_kind != N1G_ATA_TRANSFER_DMA_READ &&
         s->disk.transfer_kind != N1G_ATA_TRANSFER_DMA_WRITE)) {
        return;
    }
    s->disk.dma_pending = false;

    size_t bytes = (size_t)s->disk.dma_length + 4u;
    size_t available = (size_t)s->disk.sectors_remaining * 512u - s->disk.data_index;
    if (bytes > available) {
        bytes = available;
    }

    uint8_t *ram = n1g_ram_ptr(s, s->disk.dma_addr, bytes);
    size_t disk_off = (size_t)s->disk.transfer_lba * 512u + s->disk.data_index;
    if (!ram || bytes == 0) {
        s->disk.dma_status = 1;
        s->disk.error = 0x04u;
        s->disk.status = ATA_DRDY | ATA_ERR;
        s->disk.transfer_kind = N1G_ATA_TRANSFER_NONE;
        set_ide_irq(s, true);
        return;
    }

    if (s->disk.transfer_kind == N1G_ATA_TRANSFER_DMA_READ) {
        size_t copied = 0;
        if (s->disk.data && disk_off < s->disk.size) {
            copied = s->disk.size - disk_off;
            if (copied > bytes) {
                copied = bytes;
            }
            memcpy(ram, s->disk.data + disk_off, copied);
        }
        if (copied < bytes) {
            memset(ram + copied, 0xff, bytes - copied);
        }
        s->counters.disk_reads += (bytes + 1u) / 2u;
    } else if (s->disk.data && disk_off < s->disk.size) {
        size_t copied = s->disk.size - disk_off;
        if (copied > bytes) {
            copied = bytes;
        }
        memcpy(s->disk.data + disk_off, ram, copied);
        s->counters.disk_writes += (copied + 1u) / 2u;
    }

    s->disk.dma_addr += (uint32_t)bytes;
    s->disk.dma_length = 0;
    s->disk.dma_status = 0;
    s->disk.dma_control &= ~0x80000000u;

    size_t total = (size_t)s->disk.data_index + bytes;
    uint32_t sectors_done = (uint32_t)(total / 512u);
    s->disk.data_index = (uint16_t)(total % 512u);
    s->disk.transfer_lba += sectors_done;
    if (sectors_done >= s->disk.sectors_remaining) {
        s->disk.sectors_remaining = 0;
    } else {
        s->disk.sectors_remaining -= (uint16_t)sectors_done;
    }

    if (s->disk.sectors_remaining == 0) {
        finish_transfer(s);
    }
}

static void start_dma(n1g_state_t *s, bool write) {
    uint16_t count = s->disk.sector_count == 0 ? 256u : s->disk.sector_count;
    s->disk.error = 0;
    s->disk.transfer_lba = s->disk.selected_lba;
    s->disk.sectors_remaining = count;
    s->disk.data_index = 0;
    s->disk.transfer_kind = write ? N1G_ATA_TRANSFER_DMA_WRITE : N1G_ATA_TRANSFER_DMA_READ;
    s->disk.status = ATA_DRDY | ATA_DSC | ATA_DRQ;
    set_ide_irq(s, false);
    schedule_dma(s);
}
void n1g_disk_tick(n1g_state_t *s) {
    if (s->disk.dma_pending) {
        complete_dma_transfer(s);
    }
}

static void complete_nondata_command(n1g_state_t *s) {
    uint8_t feature = s->disk.error;
    s->disk.error = 0;
    s->disk.status = ATA_DRDY | ATA_DSC;
    s->disk.transfer_kind = N1G_ATA_TRANSFER_NONE;
    s->disk.data_index = 0;
    s->disk.sectors_remaining = 0;
    set_ide_irq(s, true);
    if (s->opts.verbose) {
        n1g_info(s, "ata command complete cmd=0x%02x feature=0x%02x count=%u lba=%u status=0x%02x",
                 s->disk.command,
                 feature,
                 s->disk.sector_count,
                 s->disk.selected_lba,
                 s->disk.status);
    }
}

static bool normalize_taskfile_offset(uint32_t offset, uint32_t *out) {
    switch (offset) {
    case PP_ATA_DATA:
        *out = 0x00u;
        return true;
    case PP_ATA_ERROR_FEATURES:
        *out = 0x04u;
        return true;
    case PP_ATA_SECTOR_COUNT:
        *out = 0x08u;
        return true;
    case PP_ATA_LBA_LOW:
        *out = 0x0cu;
        return true;
    case PP_ATA_LBA_MID:
        *out = 0x10u;
        return true;
    case PP_ATA_LBA_HIGH:
        *out = 0x14u;
        return true;
    case PP_ATA_DEVICE:
        *out = 0x18u;
        return true;
    case PP_ATA_COMMAND_STATUS:
        *out = 0x1cu;
        return true;
    default:
        if (offset <= 0x1cu) {
            *out = offset;
            return true;
        }
        return false;
    }
}

uint32_t n1g_disk_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    uint32_t reg = 0;
    if (offset == PP_IDE0_CFG) {
        return (s->disk.ide0_cfg & ~0x18u) | (s->disk.irq_pending ? 0x18u : 0u);
    }
    if (offset == PP_IDE1_CFG) {
        return s->disk.ide1_cfg;
    }
    if (offset == PP_ATA_ALT_STATUS) {
        return s->disk.status;
    }
    if (offset == PP_IDE_DMA_CONTROL) {
        return s->disk.dma_control;
    }
    if (offset == PP_IDE_DMA_STATUS) {
        return s->disk.dma_status;
    }
    if (offset == PP_IDE_DMA_LENGTH) {
        return s->disk.dma_length;
    }
    if (offset == PP_IDE_DMA_ADDR) {
        return s->disk.dma_addr;
    }
    if (offset == PP_IDE_DMA_UNKNOWN) {
        return s->disk.dma_unknown;
    }
    if (!normalize_taskfile_offset(offset, &reg)) {
        return 0;
    }

    switch (reg) {
    case 0x00:
        return data_read(s, size);
    case 0x04:
        return s->disk.error;
    case 0x08:
        return s->disk.sector_count;
    case 0x0c:
        return s->disk.selected_lba & 0xffu;
    case 0x10:
        return (s->disk.selected_lba >> 8) & 0xffu;
    case 0x14:
        return (s->disk.selected_lba >> 16) & 0xffu;
    case 0x18:
        return s->disk.device;
    case 0x1c: {
        uint8_t status = s->disk.status;
        set_ide_irq(s, false);
        return status;
    }
    default:
        return 0;
    }
}

void n1g_disk_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    uint32_t reg = 0;
    if (offset == PP_IDE0_CFG) {
        if ((value & 0x18u) != 0) {
            set_ide_irq(s, false);
        }
        s->disk.ide0_cfg = value & ~0x18u;
        return;
    }
    if (offset == PP_IDE1_CFG) {
        s->disk.ide1_cfg = value | 0x08u;
        return;
    }
    if (offset == PP_ATA_ALT_STATUS) {
        s->disk.device_control = (uint8_t)value;
        set_ide_irq(s, s->disk.irq_pending);
        return;
    }
    if (offset == PP_IDE_DMA_CONTROL) {
        s->disk.dma_control = value;
        schedule_dma(s);
        return;
    }
    if (offset == PP_IDE_DMA_STATUS) {
        s->disk.dma_status = value;
        return;
    }
    if (offset == PP_IDE_DMA_LENGTH) {
        s->disk.dma_length = value;
        return;
    }
    if (offset == PP_IDE_DMA_ADDR) {
        s->disk.dma_addr = value;
        return;
    }
    if (offset == PP_IDE_DMA_UNKNOWN) {
        s->disk.dma_unknown = value;
        return;
    }
    if (!normalize_taskfile_offset(offset, &reg)) {
        return;
    }

    switch (reg) {
    case 0x00:
        data_write(s, size, value);
        break;
    case 0x04:
        s->disk.error = (uint8_t)value;
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
        s->disk.device = (uint8_t)value;
        s->disk.selected_lba = (s->disk.selected_lba & 0x00ffffffu) | ((value & 0x0fu) << 24);
        break;
    case 0x1c:
        s->disk.command = (uint8_t)value;
        if (s->disk.command == ATA_CMD_IDENTIFY_DEVICE) {
            start_identify(s);
        } else if (s->disk.command == ATA_CMD_READ_SECTORS ||
                   s->disk.command == ATA_CMD_READ_SECTORS_EXT ||
                   s->disk.command == ATA_CMD_READ_MULTIPLE) {
            start_read(s);
        } else if (s->disk.command == ATA_CMD_WRITE_SECTORS ||
                   s->disk.command == ATA_CMD_WRITE_SECTORS_EXT ||
                   s->disk.command == ATA_CMD_WRITE_MULTIPLE) {
            start_write(s);
        } else if (s->disk.command == ATA_CMD_READ_DMA ||
                   s->disk.command == ATA_CMD_READ_DMA_NO_RETRY) {
            start_dma(s, false);
        } else if (s->disk.command == ATA_CMD_WRITE_DMA ||
                   s->disk.command == ATA_CMD_WRITE_DMA_NO_RETRY) {
            start_dma(s, true);
        } else if (s->disk.command == ATA_CMD_SET_FEATURES ||
                   s->disk.command == ATA_CMD_SET_MULTIPLE_MODE ||
                   s->disk.command == ATA_CMD_STANDBY_IMMEDIATE ||
                   s->disk.command == ATA_CMD_FLUSH_CACHE) {
            complete_nondata_command(s);
        } else {
            s->disk.error = 0x04u;
            s->disk.status = ATA_DRDY | ATA_ERR;
            s->disk.transfer_kind = N1G_ATA_TRANSFER_NONE;
            set_ide_irq(s, true);
            if (s->opts.verbose) {
                n1g_info(s, "ata unsupported cmd=0x%02x lba=%u count=%u status=0x%02x",
                         s->disk.command,
                         s->disk.selected_lba,
                         s->disk.sector_count,
                         s->disk.status);
            }
        }
        break;
    default:
        break;
    }
}
