#include "nano1g/unicorn_compat.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    VADDR_LOW = 0x00000000u,
    PADDR_CODE = 0x01000000u,
    PADDR_DATA = 0x02000000u,
    PAGE_SIZE = 0x1000u
};

typedef struct tlb_probe {
    uint32_t fetch_fills;
    uint32_t read_fills;
    uint32_t write_fills;
} tlb_probe_t;

static bool tlb_fill_cb(uc_engine *uc,
                        uint64_t vaddr,
                        uc_mem_type type,
                        uc_tlb_entry *result,
                        void *user_data) {
    (void)uc;
    tlb_probe_t *probe = (tlb_probe_t *)user_data;
    uint64_t page_offset = vaddr & (PAGE_SIZE - 1u);

    switch (type) {
    case UC_MEM_FETCH:
        probe->fetch_fills++;
        result->paddr = PADDR_CODE + page_offset;
        result->perms = UC_PROT_EXEC;
        return true;
    case UC_MEM_READ:
        probe->read_fills++;
        result->paddr = PADDR_DATA + page_offset;
        result->perms = UC_PROT_READ;
        return true;
    case UC_MEM_WRITE:
        probe->write_fills++;
        result->paddr = PADDR_DATA + page_offset;
        result->perms = UC_PROT_WRITE;
        return true;
    default:
        return false;
    }
}

static int fail_uc(uc_err err, const char *what) {
    if (err == UC_ERR_OK) {
        return 0;
    }
    fprintf(stderr, "%s: %s\n", what, uc_strerror(err));
    return 1;
}

int main(void) {
    uc_engine *uc = NULL;
    int failed = 0;
    tlb_probe_t probe;
    memset(&probe, 0, sizeof(probe));

    failed |= fail_uc(uc_open(UC_ARCH_ARM, UC_MODE_ARM | UC_MODE_LITTLE_ENDIAN, &uc), "uc_open");
    if (failed) {
        return 1;
    }

    failed |= fail_uc(uc_mem_map(uc, PADDR_CODE, PAGE_SIZE, UC_PROT_ALL), "map code");
    failed |= fail_uc(uc_mem_map(uc, PADDR_DATA, PAGE_SIZE, UC_PROT_ALL), "map data");
    failed |= fail_uc(uc_ctl_tlb_mode(uc, UC_TLB_VIRTUAL), "enable virtual tlb");

    uint32_t code[] = {
        0xe3a00000u, /* mov r0, #0 */
        0xe5901000u, /* ldr r1, [r0] */
        0xe5801004u, /* str r1, [r0, #4] */
    };
    uint32_t data0 = 0x76543210u;
    failed |= fail_uc(uc_mem_write(uc, PADDR_CODE, code, sizeof(code)), "write code");
    failed |= fail_uc(uc_mem_write(uc, PADDR_DATA, &data0, sizeof(data0)), "write data");

    uc_hook hook;
    failed |= fail_uc(uc_hook_add(uc,
                                  &hook,
                                  UC_HOOK_TLB_FILL,
                                  (void *)tlb_fill_cb,
                                  &probe,
                                  VADDR_LOW,
                                  VADDR_LOW + PAGE_SIZE - 1u),
                      "add tlb hook");

    failed |= fail_uc(uc_emu_start(uc, VADDR_LOW, UINT64_MAX, 0, 3), "emu");

    uint32_t r1 = 0;
    uint32_t stored = 0;
    failed |= fail_uc(uc_reg_read(uc, UC_ARM_REG_R1, &r1), "read r1");
    failed |= fail_uc(uc_mem_read(uc, PADDR_DATA + 4u, &stored, sizeof(stored)), "read stored");

    if (r1 != data0 || stored != data0 || probe.fetch_fills == 0 || probe.read_fills == 0 ||
        probe.write_fills == 0) {
        fprintf(stderr,
                "unexpected tlb result r1=0x%08x stored=0x%08x fills f=%u r=%u w=%u\n",
                r1,
                stored,
                probe.fetch_fills,
                probe.read_fills,
                probe.write_fills);
        failed = 1;
    }

    uc_close(uc);
    return failed ? 1 : 0;
}
