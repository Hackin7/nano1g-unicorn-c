#include "nano1g/devices.h"

#include <string.h>

static uint32_t data32(const n1g_state_t *s) {
    return (uint32_t)s->i2c.data[0] |
           ((uint32_t)s->i2c.data[1] << 8u) |
           ((uint32_t)s->i2c.data[2] << 16u) |
           ((uint32_t)s->i2c.data[3] << 24u);
}

static uint32_t mask_for_size(uint32_t size) {
    if (size == 1u) return 0xffu;
    if (size == 2u) return 0xffffu;
    return 0xffffffffu;
}

static uint32_t read_part(uint32_t value, uint32_t offset, uint32_t size) {
    uint32_t shift = (offset & 3u) * 8u;
    return (value >> shift) & mask_for_size(size);
}

static void write_data_bytes(n1g_state_t *s, uint32_t first, uint32_t size, uint32_t value) {
    for (uint32_t i = 0; i < size && first + i < sizeof(s->i2c.data); i++) {
        s->i2c.data[first + i] = (uint8_t)(value >> (i * 8u));
    }
}

static uint8_t pcf_read(n1g_state_t *s) {
    uint8_t reg = s->i2c.pcf_reg_set ? s->i2c.pcf_reg++ : 0;
    s->i2c.pcf_reg_set = true;

    if (reg < sizeof(s->i2c.pcf_regs) && (s->i2c.pcf_written & (1ull << reg)) != 0) {
        return s->i2c.pcf_regs[reg];
    }

    switch (reg) {
    case 0x00: /* ID */
    case 0x02: /* INT1 */
    case 0x03: /* INT2 */
    case 0x04: /* INT3 */
        return 0;
    case 0x0a: /* RTCSC */
        return 0x00;
    case 0x0b: /* RTCMN */
        return 0x00;
    case 0x0c: /* RTCHR */
        return 0x12;
    case 0x0d: /* RTCWD */
        return 0x02;
    case 0x0e: /* RTCDT */
        return 0x01;
    case 0x0f: /* RTCMT */
        return 0x01;
    case 0x10: /* RTCYR */
        return 0x26;
    case 0x31: /* ADCS2: ready plus low battery-voltage bits. */
        return 0x82;
    case 0x38: /* GPOC1 default used by Clicky for this PMU family. */
        return 0x04;
    default:
        if (reg < sizeof(s->i2c.pcf_regs)) {
            return s->i2c.pcf_regs[reg];
        }
        return 0;
    }
}

static void pcf_write(n1g_state_t *s, uint8_t value, bool first_byte) {
    if (first_byte) {
        s->i2c.pcf_reg = value;
        s->i2c.pcf_reg_set = true;
        return;
    }
    if (s->i2c.pcf_reg < sizeof(s->i2c.pcf_regs)) {
        s->i2c.pcf_regs[s->i2c.pcf_reg] = value;
        s->i2c.pcf_written |= 1ull << s->i2c.pcf_reg;
    }
    s->i2c.pcf_reg++;
}

static void start_txn(n1g_state_t *s) {
    uint8_t addr = (uint8_t)((s->i2c.addr_op >> 1u) & 0x7fu);
    bool read = (s->i2c.addr_op & 1u) != 0u;
    bool ctrl_read = (s->i2c.control & 0x20u) != 0u;
    uint8_t len = (uint8_t)((s->i2c.control >> 1u) & 0x03u);
    uint8_t count = (uint8_t)(len + 1u);

    if (read != ctrl_read) {
        read = ctrl_read;
    }

    if (addr == 0x08u) {
        for (uint8_t i = 0; i < count && i < sizeof(s->i2c.data); i++) {
            if (read) {
                s->i2c.data[i] = pcf_read(s);
            } else {
                pcf_write(s, s->i2c.data[i], i == 0u);
            }
        }
    } else if (read) {
        memset(s->i2c.data, 0, sizeof(s->i2c.data));
    }

    s->i2c.busy_reads = 1;
}

uint32_t n1g_dev_i2c_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    switch (offset) {
    case 0x00:
        return read_part(s->i2c.control & 0x26u, offset, size);
    case 0x04:
        return read_part(s->i2c.addr_op, offset, size);
    case 0x0c:
        return read_part(data32(s), offset, size);
    case 0x10:
        return read_part(s->i2c.data[1], offset, size);
    case 0x14:
        return read_part(s->i2c.data[2], offset, size);
    case 0x18:
        return read_part(s->i2c.data[3], offset, size);
    case 0x1c:
        if (s->i2c.busy_reads) {
            s->i2c.busy_reads--;
            return 0x40u;
        }
        return 0;
    default:
        if (offset < sizeof(s->i2c.regs)) {
            return s->i2c.regs[offset / 4u];
        }
        return 0;
    }
}

void n1g_dev_i2c_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    uint8_t v = (uint8_t)value;
    switch (offset) {
    case 0x00:
        s->i2c.control = v;
        if (v & 0x80u) {
            start_txn(s);
        }
        break;
    case 0x04:
        s->i2c.addr_op = v;
        break;
    case 0x0c:
        write_data_bytes(s, 0, size, value);
        break;
    case 0x10:
        write_data_bytes(s, 1, size, value);
        break;
    case 0x14:
        write_data_bytes(s, 2, size, value);
        break;
    case 0x18:
        write_data_bytes(s, 3, size, value);
        break;
    default:
        if (offset < sizeof(s->i2c.regs)) {
            s->i2c.regs[offset / 4u] = value;
        }
        break;
    }
}
