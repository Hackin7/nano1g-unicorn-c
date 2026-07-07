#include "nano1g/cpu_unicorn.h"

#include "nano1g/bus.h"
#include "nano1g/ram.h"
#include "nano1g/trace.h"

#include <stdlib.h>
#include <string.h>

typedef struct mmio_ctx {
    n1g_state_t *s;
    uint32_t base;
} mmio_ctx_t;

static const int arm_regs[16] = {
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
    UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
};

static uint64_t mmio_read_cb(uc_engine *uc, uint64_t offset, unsigned size, void *user_data) {
    (void)uc;
    mmio_ctx_t *ctx = (mmio_ctx_t *)user_data;
    uint64_t addr = offset < ctx->base ? (uint64_t)ctx->base + offset : offset;
    return n1g_bus_read(ctx->s, (uint32_t)addr, size);
}

static void mmio_write_cb(uc_engine *uc, uint64_t offset, unsigned size, uint64_t value, void *user_data) {
    (void)uc;
    mmio_ctx_t *ctx = (mmio_ctx_t *)user_data;
    uint64_t addr = offset < ctx->base ? (uint64_t)ctx->base + offset : offset;
    n1g_bus_write(ctx->s, (uint32_t)addr, size, (uint32_t)value);
}

static void hook_block(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    (void)uc;
    n1g_state_t *s = (n1g_state_t *)user_data;
    if (s->opts.trace_pc) {
        n1g_log(s, "block pc=0x%08llx size=%u", (unsigned long long)address, size);
    }
}

static bool map_ram_one(uc_engine *uc, uint64_t addr, size_t size, void *ptr) {
    uc_err err = uc_mem_map_ptr(uc, addr, size, UC_PROT_ALL, ptr);
    if (err == UC_ERR_OK) {
        return true;
    }
    err = uc_mem_map(uc, addr, size, UC_PROT_ALL);
    if (err != UC_ERR_OK) {
        return false;
    }
    return uc_mem_write(uc, addr, ptr, size) == UC_ERR_OK;
}

static bool map_mmio(n1g_state_t *s, uc_engine *uc, uint32_t base, size_t size) {
    if (s->mmio_context_count >= sizeof(s->mmio_contexts) / sizeof(s->mmio_contexts[0])) {
        return false;
    }
    mmio_ctx_t *ctx = (mmio_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        return false;
    }
    ctx->s = s;
    ctx->base = base;
    s->mmio_contexts[s->mmio_context_count++] = ctx;
    uc_err err = uc_mmio_map(uc, base, size, mmio_read_cb, ctx, mmio_write_cb, ctx);
    if (err != UC_ERR_OK) {
        n1g_log(s, "uc_mmio_map failed base=0x%08x size=0x%zx: %s", base, size, uc_strerror(err));
        return false;
    }
    return true;
}

bool n1g_cpu_init(n1g_state_t *s) {
    for (int i = 0; i < N1G_CORE_COUNT; i++) {
        uc_err err = uc_open(UC_ARCH_ARM, UC_MODE_ARM | UC_MODE_LITTLE_ENDIAN, &s->cpu[i].uc);
        if (err != UC_ERR_OK) {
            n1g_log(s, "uc_open failed: %s", uc_strerror(err));
            return false;
        }
        s->cpu[i].running = true;
        s->cpu[i].halted = false;
        uc_hook hh;
        uc_hook_add(s->cpu[i].uc, &hh, UC_HOOK_BLOCK, hook_block, s, 1, 0);
    }
    return true;
}

void n1g_cpu_destroy(n1g_state_t *s) {
    for (int i = 0; i < N1G_CORE_COUNT; i++) {
        if (s->cpu[i].uc) {
            uc_close(s->cpu[i].uc);
            s->cpu[i].uc = NULL;
        }
    }
    for (size_t i = 0; i < s->mmio_context_count; i++) {
        free(s->mmio_contexts[i]);
        s->mmio_contexts[i] = NULL;
    }
    s->mmio_context_count = 0;
}

bool n1g_cpu_map_memory(n1g_state_t *s) {
    for (int c = 0; c < N1G_CORE_COUNT; c++) {
        uc_engine *uc = s->cpu[c].uc;
        if (!map_ram_one(uc, N1G_SDRAM_BASE, N1G_SDRAM_SIZE, s->ram.sdram)) return false;
        if (!map_ram_one(uc, N1G_SDRAM_BASE + N1G_SDRAM_SIZE, N1G_SDRAM_SIZE, s->ram.sdram)) return false;
        if (!map_ram_one(uc, N1G_SDRAM_BASE + 2u * N1G_SDRAM_SIZE, N1G_SDRAM_SIZE, s->ram.sdram)) return false;
        if (!map_ram_one(uc, N1G_SDRAM_BASE + 3u * N1G_SDRAM_SIZE, N1G_SDRAM_SIZE, s->ram.sdram)) return false;
        if (!map_ram_one(uc, N1G_FASTRAM_BASE, N1G_FASTRAM_SIZE, s->ram.fastram)) return false;

        if (!map_mmio(s, uc, N1G_FLASH_BASE, N1G_FLASH_SIZE)) return false;
        if (!map_mmio(s, uc, N1G_PP_BASE, 0x00100000u)) return false;
        if (!map_mmio(s, uc, 0x64000000u, 0x00010000u)) return false;
        if (!map_mmio(s, uc, N1G_PPCON_BASE, 0x00010000u)) return false;
        if (!map_mmio(s, uc, N1G_EIDE_BASE, 0x00001000u)) return false;
        if (!map_mmio(s, uc, N1G_MEMCON_BASE, 0x00010000u)) return false;
    }
    return true;
}

bool n1g_cpu_step_slice(n1g_state_t *s, n1g_core_t core, uint32_t max_insns) {
    if (s->cpu[core].halted || !s->cpu[core].running) {
        return true;
    }
    uint32_t pc = n1g_cpu_pc(s, core);
    uc_err err = uc_emu_start(s->cpu[core].uc, pc, 0, 0, max_insns);
    if (err != UC_ERR_OK) {
        n1g_log(s, "uc_emu_start core=%d pc=0x%08x failed: %s", core, pc, uc_strerror(err));
        return false;
    }
    s->cpu[core].guest_insns += max_insns;
    s->counters.guest_insns += max_insns;
    return true;
}

void n1g_cpu_raise_irq(n1g_state_t *s, n1g_core_t core) {
    s->cpu[core].halted = false;
    s->counters.irq_count++;
    uc_emu_stop(s->cpu[core].uc);
}

uint32_t n1g_cpu_pc(n1g_state_t *s, n1g_core_t core) {
    uint64_t v = 0;
    uc_reg_read(s->cpu[core].uc, UC_ARM_REG_PC, &v);
    return (uint32_t)v;
}

void n1g_cpu_set_reg(n1g_state_t *s, n1g_core_t core, int reg, uint32_t value) {
    uint64_t v = value;
    int regid = (reg >= 0 && reg < 16) ? arm_regs[reg] : reg;
    uc_reg_write(s->cpu[core].uc, regid, &v);
}

uint32_t n1g_cpu_get_reg(n1g_state_t *s, n1g_core_t core, int reg) {
    uint64_t v = 0;
    int regid = (reg >= 0 && reg < 16) ? arm_regs[reg] : reg;
    uc_reg_read(s->cpu[core].uc, regid, &v);
    return (uint32_t)v;
}
