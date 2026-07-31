#include "nano1g/cpu_unicorn.h"

#include "nano1g/bus.h"
#include "nano1g/devices.h"
#include "nano1g/memmap.h"
#include "nano1g/ram.h"
#include "nano1g/trace.h"

#include <stdlib.h>
#include <string.h>

typedef struct mmio_ctx {
    n1g_state_t *s;
    uint32_t base;
    n1g_core_t core;
} mmio_ctx_t;

static const int arm_regs[16] = {
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
    UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
};

static uint64_t mmio_read_cb(uc_engine *uc, uint64_t offset, unsigned size, void *user_data) {
    mmio_ctx_t *ctx = (mmio_ctx_t *)user_data;
    uint64_t addr = offset < ctx->base ? (uint64_t)ctx->base + offset : offset;
    if (ctx->s->opts.trace_mmio) {
        uint32_t pc = 0;
        uc_reg_read(uc, UC_ARM_REG_PC, &pc);
        n1g_log(ctx->s, "mmio read%u pc=0x%08x addr=0x%08x",
                size * 8u, pc, (uint32_t)addr);
    }
    return n1g_bus_read(ctx->s, ctx->core, (uint32_t)addr, size);
}

static void mmio_write_cb(uc_engine *uc, uint64_t offset, unsigned size, uint64_t value, void *user_data) {
    mmio_ctx_t *ctx = (mmio_ctx_t *)user_data;
    uint64_t addr = offset < ctx->base ? (uint64_t)ctx->base + offset : offset;
    if (ctx->s->opts.trace_mmio) {
        uint32_t pc = 0;
        uc_reg_read(uc, UC_ARM_REG_PC, &pc);
        n1g_log(ctx->s, "mmio write%u pc=0x%08x addr=0x%08x value=0x%08x",
                size * 8u, pc, (uint32_t)addr, (uint32_t)value);
    }
    n1g_bus_write_core(ctx->s, ctx->core, (uint32_t)addr, size, (uint32_t)value);
    if (ctx->s->tb_flush_pending) {
        /* Stop this core at the current instruction so the pending TB flush
         * can run safely outside of emulation. Unicorn leaves PC at the MMIO
         * instruction when uc_emu_stop() is called from the hook, so advance
         * past this store or cache-maintenance writes repeat forever. */
        uint32_t pc = 0;
        uint32_t cpsr = 0;
        uc_reg_read(uc, UC_ARM_REG_PC, &pc);
        uc_reg_read(uc, UC_ARM_REG_CPSR, &cpsr);
        pc += (cpsr & 0x20u) ? 2u : 4u;
        uc_reg_write(uc, UC_ARM_REG_PC, &pc);
        uc_emu_stop(uc);
    }
}

static bool tlb_access_from_type(uc_mem_type type, n1g_mmap_access_t *access, uc_prot *perms) {
    switch (type) {
    case UC_MEM_READ:
        *access = N1G_MMAP_ACCESS_READ_DATA;
        *perms = UC_PROT_READ;
        return true;
    case UC_MEM_WRITE:
        *access = N1G_MMAP_ACCESS_WRITE_DATA;
        *perms = UC_PROT_WRITE;
        return true;
    case UC_MEM_FETCH:
        *access = N1G_MMAP_ACCESS_FETCH_CODE;
        *perms = UC_PROT_EXEC;
        return true;
    default:
        return false;
    }
}

static void decode_current_mmaps(n1g_state_t *s, n1g_mmap_entry_t entries[8]) {
    for (uint32_t i = 0; i < 8u; i++) {
        uint32_t logical = s->memcon.regs[(0xf000u / 4u) + i * 2u];
        uint32_t physical = s->memcon.regs[(0xf004u / 4u) + i * 2u];
        entries[i] = n1g_mmap_decode(logical, physical);
    }
}

static bool hook_tlb_fill(uc_engine *uc,
                          uint64_t vaddr,
                          uc_mem_type type,
                          uc_tlb_entry *result,
                          void *user_data) {
    (void)uc;
    n1g_state_t *s = (n1g_state_t *)user_data;
    n1g_mmap_access_t access;
    uc_prot perms;
    uint32_t translated = (uint32_t)vaddr;
    n1g_mmap_entry_t entries[8];

    if (!s->opts.virtual_memmap || !tlb_access_from_type(type, &access, &perms)) {
        return false;
    }

    decode_current_mmaps(s, entries);
    (void)n1g_mmap_translate(entries, 8u, (uint32_t)vaddr, access, &translated);

    result->paddr = translated;
    result->perms = perms;
    return true;
}

static void hook_block(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    n1g_state_t *s = (n1g_state_t *)user_data;
    if (s->opts.profile == N1G_PROFILE_APPLE &&
        (address == 0x0002a574u || address == 0x0002a8b0u)) {
        uint32_t r0 = 0;
        uint32_t lr = 0;
        uc_reg_read(uc, UC_ARM_REG_R0, &r0);
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);
        if ((r0 & 0xff000000u) != 0u) {
            static uint32_t suspicious_id_logs;
            if (suspicious_id_logs < 16u) {
                suspicious_id_logs++;
                n1g_log(s, "apple object-table suspicious id pc=0x%08llx r0=0x%08x lr=0x%08x",
                        (unsigned long long)address, r0, lr);
            }
        }
    }
    if (s->opts.trace_pc) {
        n1g_log(s, "block pc=0x%08llx size=%u", (unsigned long long)address, size);
    }
}

static int core_index_from_uc(n1g_state_t *s, uc_engine *uc) {
    for (int i = 0; i < N1G_CORE_COUNT; i++) {
        if (s->cpu[i].uc == uc) {
            return i;
        }
    }
    return -1;
}

static bool read_guest_byte(uc_engine *uc, uint32_t addr, uint8_t *out) {
    return uc_mem_read(uc, addr, out, sizeof(*out)) == UC_ERR_OK;
}

static void log_swi_diagnostic(uc_engine *uc, n1g_state_t *s, uint32_t insn) {
    if (!s->opts.verbose || insn != 0xef123456u) {
        return;
    }

    uint32_t op = 0;
    uint32_t ptr = 0;
    uc_reg_read(uc, UC_ARM_REG_R0, &op);
    uc_reg_read(uc, UC_ARM_REG_R1, &ptr);

    static uint32_t logs;
    if (logs >= 80u) {
        return;
    }
    logs++;

    if (op == 3u) {
        uint8_t ch = 0;
        if (read_guest_byte(uc, ptr, &ch)) {
            n1g_log(s, "swi diag char 0x%02x '%c'", ch, ch >= 32u && ch < 127u ? (char)ch : '.');
        } else {
            n1g_log(s, "swi diag char unreadable ptr=0x%08x", ptr);
        }
        return;
    }

    if (op == 4u) {
        char text[129];
        uint32_t i = 0;
        for (; i + 1u < sizeof(text); i++) {
            uint8_t ch = 0;
            if (!read_guest_byte(uc, ptr + i, &ch) || ch == 0) {
                break;
            }
            text[i] = (ch >= 32u && ch < 127u) ? (char)ch : '.';
        }
        text[i] = '\0';
        n1g_log(s, "swi diag string ptr=0x%08x \"%s\"", ptr, text);
        return;
    }

    n1g_log(s, "swi diag op=%u ptr=0x%08x", op, ptr);
}

static bool enter_swi_exception(uc_engine *uc, n1g_state_t *s, uint32_t pc, uint32_t intno) {
    uint32_t insn = 0;
    uint32_t lr_svc = 0;

    if (uc_mem_read(uc, pc, &insn, sizeof(insn)) == UC_ERR_OK && (insn & 0xff000000u) == 0xef000000u) {
        lr_svc = pc + 4u;
    } else if (pc >= 4u &&
               uc_mem_read(uc, pc - 4u, &insn, sizeof(insn)) == UC_ERR_OK &&
               (insn & 0xff000000u) == 0xef000000u) {
        lr_svc = pc;
    } else {
        return false;
    }

    log_swi_diagnostic(uc, s, insn);

    uint32_t old_cpsr = 0;
    uc_reg_read(uc, UC_ARM_REG_CPSR, &old_cpsr);
    uint32_t svc_cpsr = (old_cpsr & ~0x3fu) | 0x93u;
    uint32_t vector = (s->cachecon.regs[0] & 0x10u) ? s->evp.regs[2] : 0x08u;

    uc_reg_write(uc, UC_ARM_REG_CPSR, &svc_cpsr);
    uc_reg_write(uc, UC_ARM_REG_SPSR, &old_cpsr);
    uc_reg_write(uc, UC_ARM_REG_LR, &lr_svc);
    uc_reg_write(uc, UC_ARM_REG_PC, &vector);

    if (s->opts.trace_mmio || s->opts.trace_pc || s->opts.verbose) {
        int core = core_index_from_uc(s, uc);
        n1g_log(s,
                "swi exception core=%d int=%u insn=0x%08x lr=0x%08x vector=0x%08x cpsr=0x%08x",
                core,
                intno,
                insn,
                lr_svc,
                vector,
                old_cpsr);
    }
    return true;
}

static void hook_intr(uc_engine *uc, uint32_t intno, void *user_data) {
    n1g_state_t *s = (n1g_state_t *)user_data;
    uint32_t pc = 0;
    uint32_t insn = 0;
    uc_reg_read(uc, UC_ARM_REG_PC, &pc);

    if (enter_swi_exception(uc, s, pc, intno)) {
        return;
    }

    n1g_log(s, "unhandled interrupt int=%u pc=0x%08x insn=0x%08x", intno, pc, insn);
}

static uint32_t ram_read32_or_zero(n1g_state_t *s, uint32_t addr) {
    uint32_t value = 0;
    (void)n1g_ram_read(s, addr, 4, &value);
    return value;
}

static void record_apple_lcd_path_probe(uc_engine *uc, n1g_state_t *s, uint32_t slot) {
    if (slot >= sizeof(s->counters.apple_lcd_path_hits) / sizeof(s->counters.apple_lcd_path_hits[0])) {
        return;
    }

    s->counters.apple_lcd_path_hits[slot]++;
    uc_reg_read(uc, UC_ARM_REG_R0, &s->counters.apple_lcd_path_last[slot][0]);
    uc_reg_read(uc, UC_ARM_REG_R1, &s->counters.apple_lcd_path_last[slot][1]);
    uc_reg_read(uc, UC_ARM_REG_R2, &s->counters.apple_lcd_path_last[slot][2]);
    uc_reg_read(uc, UC_ARM_REG_R3, &s->counters.apple_lcd_path_last[slot][3]);
    uc_reg_read(uc, UC_ARM_REG_R4, &s->counters.apple_lcd_path_last[slot][4]);
    uc_reg_read(uc, UC_ARM_REG_LR, &s->counters.apple_lcd_path_last[slot][5]);
}

static void record_apple_lcd_producer_probe(uc_engine *uc, n1g_state_t *s, uint32_t slot) {
    if (slot >= sizeof(s->counters.apple_lcd_producer_hits) / sizeof(s->counters.apple_lcd_producer_hits[0])) {
        return;
    }

    s->counters.apple_lcd_producer_hits[slot]++;
    uc_reg_read(uc, UC_ARM_REG_R0, &s->counters.apple_lcd_producer_last[slot][0]);
    uc_reg_read(uc, UC_ARM_REG_R1, &s->counters.apple_lcd_producer_last[slot][1]);
    uc_reg_read(uc, UC_ARM_REG_R2, &s->counters.apple_lcd_producer_last[slot][2]);
    uc_reg_read(uc, UC_ARM_REG_R3, &s->counters.apple_lcd_producer_last[slot][3]);
    uc_reg_read(uc, UC_ARM_REG_R4, &s->counters.apple_lcd_producer_last[slot][4]);
    uc_reg_read(uc, UC_ARM_REG_R5, &s->counters.apple_lcd_producer_last[slot][5]);
    uc_reg_read(uc, UC_ARM_REG_SP, &s->counters.apple_lcd_producer_last[slot][6]);
    uc_reg_read(uc, UC_ARM_REG_LR, &s->counters.apple_lcd_producer_last[slot][7]);
}

static void record_apple_input_probe(uc_engine *uc, n1g_state_t *s, uint32_t slot) {
    if (slot >= sizeof(s->counters.apple_input_hits) / sizeof(s->counters.apple_input_hits[0])) {
        return;
    }

    uint32_t r0 = 0;
    uint32_t r1 = 0;
    uint32_t r2 = 0;
    uint32_t r3 = 0;
    uint32_t r4 = 0;
    uint32_t sp = 0;
    uint32_t lr = 0;
    uc_reg_read(uc, UC_ARM_REG_R0, &r0);
    uc_reg_read(uc, UC_ARM_REG_R1, &r1);
    uc_reg_read(uc, UC_ARM_REG_R2, &r2);
    uc_reg_read(uc, UC_ARM_REG_R3, &r3);
    uc_reg_read(uc, UC_ARM_REG_R4, &r4);
    uc_reg_read(uc, UC_ARM_REG_SP, &sp);
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);

    s->counters.apple_input_hits[slot]++;
    s->counters.apple_input_last[slot][0] = r0;
    s->counters.apple_input_last[slot][1] = r1;
    s->counters.apple_input_last[slot][2] = r2;
    s->counters.apple_input_last[slot][3] = r3;
    s->counters.apple_input_last[slot][4] = r4;
    s->counters.apple_input_last[slot][5] = sp;
    s->counters.apple_input_last[slot][6] = lr;
    s->counters.apple_input_last[slot][7] = 0;

    switch (slot) {
    case 0: /* opto ISR raw packet decode */
        s->counters.apple_input_last[slot][7] = r0;
        break;
    case 1: /* opto ISR stored decoded state */
        s->counters.apple_input_last[slot][5] = ram_read32_or_zero(s, 0x10706100u);
        s->counters.apple_input_last[slot][6] = ram_read32_or_zero(s, 0x10706104u);
        s->counters.apple_input_last[slot][7] = ram_read32_or_zero(s, 0x107060e4u);
        break;
    case 3: /* event queue post, descriptor in r0 on observed builds */
        s->counters.apple_input_last[slot][5] = ram_read32_or_zero(s, r0);
        s->counters.apple_input_last[slot][6] = ram_read32_or_zero(s, r0 + 0x1cu);
        s->counters.apple_input_last[slot][7] = ram_read32_or_zero(s, r0 + 0x30u);
        break;
    case 4: { /* event queue receive, descriptor appears in caller stack */
        uint32_t evt = ram_read32_or_zero(s, sp + 4u);
        s->counters.apple_input_last[slot][5] = evt;
        s->counters.apple_input_last[slot][6] = ram_read32_or_zero(s, evt + 0x1cu);
        s->counters.apple_input_last[slot][7] = ram_read32_or_zero(s, evt + 0x30u);
        break;
    }
    case 5:
    case 6: { /* UI queue/language loop event pointer from stack */
        uint32_t evt = ram_read32_or_zero(s, sp + 8u);
        s->counters.apple_input_last[slot][5] = evt;
        s->counters.apple_input_last[slot][6] = ram_read32_or_zero(s, evt + 0x1cu);
        s->counters.apple_input_last[slot][7] = ram_read32_or_zero(s, evt + 0x30u);
        break;
    }
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
        s->counters.apple_input_last[slot][5] = ram_read32_or_zero(s, r4 + 0x1cu);
        s->counters.apple_input_last[slot][6] = ram_read32_or_zero(s, r4 + 0x28u);
        s->counters.apple_input_last[slot][7] = ram_read32_or_zero(s, r4 + 0x30u);
        break;
    default:
        break;
    }
}

static void record_apple_input_task_probe(uc_engine *uc, n1g_state_t *s, uint32_t slot) {
    if (slot >= sizeof(s->counters.apple_input_task_hits) / sizeof(s->counters.apple_input_task_hits[0])) {
        return;
    }

    uint32_t r0 = 0;
    uint32_t r1 = 0;
    uint32_t r2 = 0;
    uint32_t r3 = 0;
    uint32_t r4 = 0;
    uint32_t r7 = 0;
    uint32_t sp = 0;
    uint32_t lr = 0;
    uc_reg_read(uc, UC_ARM_REG_R0, &r0);
    uc_reg_read(uc, UC_ARM_REG_R1, &r1);
    uc_reg_read(uc, UC_ARM_REG_R2, &r2);
    uc_reg_read(uc, UC_ARM_REG_R3, &r3);
    uc_reg_read(uc, UC_ARM_REG_R4, &r4);
    uc_reg_read(uc, UC_ARM_REG_R7, &r7);
    uc_reg_read(uc, UC_ARM_REG_SP, &sp);
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);

    s->counters.apple_input_task_hits[slot]++;
    s->counters.apple_input_task_last[slot][0] = r0;
    s->counters.apple_input_task_last[slot][1] = r1;
    s->counters.apple_input_task_last[slot][2] = r2;
    s->counters.apple_input_task_last[slot][3] = r3;
    s->counters.apple_input_task_last[slot][4] = r4;
    s->counters.apple_input_task_last[slot][5] = r7;
    s->counters.apple_input_task_last[slot][6] = sp;
    s->counters.apple_input_task_last[slot][7] = lr;

    switch (slot) {
    case 9:
    case 13:
    case 14:
        s->counters.apple_input_task_last[slot][5] = ram_read32_or_zero(s, r0);
        s->counters.apple_input_task_last[slot][6] = ram_read32_or_zero(s, r1);
        s->counters.apple_input_task_last[slot][7] = ram_read32_or_zero(s, r1 + 4u);
        break;
    case 10:
    case 11:
    case 12:
        s->counters.apple_input_task_last[slot][5] = ram_read32_or_zero(s, r7);
        s->counters.apple_input_task_last[slot][6] = ram_read32_or_zero(s, r7 + 0x10u);
        s->counters.apple_input_task_last[slot][7] = ram_read32_or_zero(s, r7 + 0x14u);
        break;
    case 15: {
        uint32_t evt = ram_read32_or_zero(s, sp + 0x1cu);
        s->counters.apple_input_task_last[slot][5] = evt;
        s->counters.apple_input_task_last[slot][6] = ram_read32_or_zero(s, evt + 0x1cu);
        s->counters.apple_input_task_last[slot][7] = ram_read32_or_zero(s, evt + 0x30u);
        break;
    }
    default:
        break;
    }
}

static void record_apple_ui_ready_probe(uc_engine *uc, n1g_state_t *s, uint32_t slot) {
    if (slot >= sizeof(s->counters.apple_ui_ready_hits) / sizeof(s->counters.apple_ui_ready_hits[0])) {
        return;
    }

    uint32_t r[6] = {0};
    uint32_t sp = 0;
    uint32_t lr = 0;
    for (int i = 0; i < 6; i++) {
        uc_reg_read(uc, arm_regs[i], &r[i]);
    }
    uc_reg_read(uc, UC_ARM_REG_SP, &sp);
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);

    s->counters.apple_ui_ready_hits[slot]++;
    s->counters.apple_ui_ready_last[slot][0] = r[0];
    s->counters.apple_ui_ready_last[slot][1] = r[1];
    s->counters.apple_ui_ready_last[slot][2] = r[2];
    s->counters.apple_ui_ready_last[slot][3] = r[3];
    s->counters.apple_ui_ready_last[slot][4] = r[4];
    s->counters.apple_ui_ready_last[slot][5] = r[5];
    s->counters.apple_ui_ready_last[slot][6] = sp;
    s->counters.apple_ui_ready_last[slot][7] = lr;
    s->counters.apple_ui_ready_bytes68 = ram_read32_or_zero(s, 0x10705468u);
    s->counters.apple_ui_ready_bytes6c = ram_read32_or_zero(s, 0x1070546cu);

    if (s->counters.apple_ui_ready_hits[slot] <= 16u) {
        n1g_log(s,
                "apple ui-ready probe slot=%u hit=%llu pc_slot r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x r4=0x%08x r5=0x%08x sp=0x%08x lr=0x%08x bytes68=0x%08x bytes6c=0x%08x",
                slot,
                (unsigned long long)s->counters.apple_ui_ready_hits[slot],
                r[0],
                r[1],
                r[2],
                r[3],
                r[4],
                r[5],
                sp,
                lr,
                s->counters.apple_ui_ready_bytes68,
                s->counters.apple_ui_ready_bytes6c);
    }
}

static void record_apple_work_pool_probe(uc_engine *uc, n1g_state_t *s, uint32_t slot) {
    if (slot >= sizeof(s->counters.apple_work_pool_hits) / sizeof(s->counters.apple_work_pool_hits[0])) {
        return;
    }

    uint32_t r[6] = {0};
    uint32_t sp = 0;
    uint32_t lr = 0;
    for (int i = 0; i < 6; i++) {
        uc_reg_read(uc, arm_regs[i], &r[i]);
    }
    uc_reg_read(uc, UC_ARM_REG_SP, &sp);
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);

    s->counters.apple_work_pool_hits[slot]++;
    s->counters.apple_work_pool_last[slot][0] = r[0];
    s->counters.apple_work_pool_last[slot][1] = r[1];
    s->counters.apple_work_pool_last[slot][2] = r[2];
    s->counters.apple_work_pool_last[slot][3] = r[3];
    s->counters.apple_work_pool_last[slot][4] = r[4];
    s->counters.apple_work_pool_last[slot][5] = r[5];
    s->counters.apple_work_pool_last[slot][6] = sp;
    s->counters.apple_work_pool_last[slot][7] = lr;
    s->counters.apple_work_pool_head = ram_read32_or_zero(s, 0x107059d0u);
    for (uint32_t i = 0; i < 4u; i++) {
        s->counters.apple_work_pool_words[i] = ram_read32_or_zero(s, 0x107059d0u + i * 4u);
    }

    if (s->counters.apple_work_pool_hits[slot] <= 32u) {
        n1g_log(s,
                "apple work-pool probe slot=%u hit=%llu r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x r4=0x%08x r5=0x%08x sp=0x%08x lr=0x%08x pool=0x%08x words=0x%08x,0x%08x,0x%08x,0x%08x",
                slot,
                (unsigned long long)s->counters.apple_work_pool_hits[slot],
                r[0],
                r[1],
                r[2],
                r[3],
                r[4],
                r[5],
                sp,
                lr,
                s->counters.apple_work_pool_head,
                s->counters.apple_work_pool_words[0],
                s->counters.apple_work_pool_words[1],
                s->counters.apple_work_pool_words[2],
                s->counters.apple_work_pool_words[3]);
    }
}

static void record_apple_ui_branch_probe(uc_engine *uc, n1g_state_t *s, uint32_t slot) {
    if (slot >= sizeof(s->counters.apple_ui_branch_hits) / sizeof(s->counters.apple_ui_branch_hits[0])) {
        return;
    }

    uint32_t r[6] = {0};
    uint32_t sp = 0;
    uint32_t lr = 0;
    for (int i = 0; i < 6; i++) {
        uc_reg_read(uc, arm_regs[i], &r[i]);
    }
    uc_reg_read(uc, UC_ARM_REG_SP, &sp);
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);

    s->counters.apple_ui_branch_hits[slot]++;
    s->counters.apple_ui_branch_last[slot][0] = r[0];
    s->counters.apple_ui_branch_last[slot][1] = r[1];
    s->counters.apple_ui_branch_last[slot][2] = r[2];
    s->counters.apple_ui_branch_last[slot][3] = r[3];
    s->counters.apple_ui_branch_last[slot][4] = r[4];
    s->counters.apple_ui_branch_last[slot][5] = r[5];
    s->counters.apple_ui_branch_last[slot][6] = sp;
    s->counters.apple_ui_branch_last[slot][7] = lr;

    uint32_t base = slot >= 3u ? r[4] : r[0];
    s->counters.apple_ui_branch_words[slot][0] = ram_read32_or_zero(s, base);
    s->counters.apple_ui_branch_words[slot][1] = ram_read32_or_zero(s, base + 4u);
    s->counters.apple_ui_branch_words[slot][2] = ram_read32_or_zero(s, base + 8u);
    s->counters.apple_ui_branch_words[slot][3] = ram_read32_or_zero(s, base + 0x1cu);

    if (s->counters.apple_ui_branch_hits[slot] <= 32u) {
        n1g_log(s,
                "apple ui-branch probe slot=%u hit=%llu r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x r4=0x%08x r5=0x%08x sp=0x%08x lr=0x%08x base=0x%08x words=0x%08x,0x%08x,0x%08x,0x%08x",
                slot,
                (unsigned long long)s->counters.apple_ui_branch_hits[slot],
                r[0],
                r[1],
                r[2],
                r[3],
                r[4],
                r[5],
                sp,
                lr,
                base,
                s->counters.apple_ui_branch_words[slot][0],
                s->counters.apple_ui_branch_words[slot][1],
                s->counters.apple_ui_branch_words[slot][2],
                s->counters.apple_ui_branch_words[slot][3]);
    }
}

static void record_apple_ui_dispatch_probe(uc_engine *uc, n1g_state_t *s, uint32_t slot) {
    if (slot >= sizeof(s->counters.apple_ui_dispatch_hits) / sizeof(s->counters.apple_ui_dispatch_hits[0])) {
        return;
    }

    uint32_t r[6] = {0};
    uint32_t sp = 0;
    uint32_t lr = 0;
    for (int i = 0; i < 6; i++) {
        uc_reg_read(uc, arm_regs[i], &r[i]);
    }
    uc_reg_read(uc, UC_ARM_REG_SP, &sp);
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);

    s->counters.apple_ui_dispatch_hits[slot]++;
    s->counters.apple_ui_dispatch_last[slot][0] = r[0];
    s->counters.apple_ui_dispatch_last[slot][1] = r[1];
    s->counters.apple_ui_dispatch_last[slot][2] = r[2];
    s->counters.apple_ui_dispatch_last[slot][3] = r[3];
    s->counters.apple_ui_dispatch_last[slot][4] = r[4];
    s->counters.apple_ui_dispatch_last[slot][5] = r[5];
    s->counters.apple_ui_dispatch_last[slot][6] = sp;
    s->counters.apple_ui_dispatch_last[slot][7] = lr;

    uint32_t obj = slot >= 4u ? r[4] : r[0];
    uint32_t vtable = ram_read32_or_zero(s, obj);
    s->counters.apple_ui_dispatch_words[slot][0] = vtable;
    s->counters.apple_ui_dispatch_words[slot][1] = ram_read32_or_zero(s, obj + 4u);
    s->counters.apple_ui_dispatch_words[slot][2] = ram_read32_or_zero(s, obj + 8u);
    s->counters.apple_ui_dispatch_words[slot][3] = ram_read32_or_zero(s, obj + 0x1cu);
    s->counters.apple_ui_dispatch_words[slot][4] = ram_read32_or_zero(s, vtable);
    s->counters.apple_ui_dispatch_words[slot][5] = ram_read32_or_zero(s, vtable + 4u);
    s->counters.apple_ui_dispatch_words[slot][6] = ram_read32_or_zero(s, vtable + 0xbcu);
    s->counters.apple_ui_dispatch_words[slot][7] = ram_read32_or_zero(s, vtable + 0x140u);

    if (s->counters.apple_ui_dispatch_hits[slot] <= 32u) {
        n1g_log(s,
                "apple ui-dispatch probe slot=%u hit=%llu r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x r4=0x%08x r5=0x%08x sp=0x%08x lr=0x%08x objw=0x%08x,0x%08x,0x%08x,0x%08x vt=0x%08x,0x%08x,0x%08x,0x%08x",
                slot,
                (unsigned long long)s->counters.apple_ui_dispatch_hits[slot],
                r[0],
                r[1],
                r[2],
                r[3],
                r[4],
                r[5],
                sp,
                lr,
                s->counters.apple_ui_dispatch_words[slot][0],
                s->counters.apple_ui_dispatch_words[slot][1],
                s->counters.apple_ui_dispatch_words[slot][2],
                s->counters.apple_ui_dispatch_words[slot][3],
                s->counters.apple_ui_dispatch_words[slot][4],
                s->counters.apple_ui_dispatch_words[slot][5],
                s->counters.apple_ui_dispatch_words[slot][6],
                s->counters.apple_ui_dispatch_words[slot][7]);
    }
}

static void format_guest_bytes(uc_engine *uc, uint32_t addr, char *hex, size_t hex_size, char *ascii, size_t ascii_size) {
    uint8_t bytes[32];
    memset(bytes, 0, sizeof(bytes));
    if (uc_mem_read(uc, addr, bytes, sizeof(bytes)) != UC_ERR_OK) {
        snprintf(hex, hex_size, "unreadable");
        snprintf(ascii, ascii_size, "unreadable");
        return;
    }

    size_t pos = 0;
    for (size_t i = 0; i < sizeof(bytes) && pos + 3u < hex_size; i++) {
        pos += (size_t)snprintf(hex + pos, hex_size - pos, "%02x", bytes[i]);
    }
    hex[hex_size - 1u] = '\0';

    size_t text_len = sizeof(bytes);
    if (text_len >= ascii_size) {
        text_len = ascii_size - 1u;
    }
    for (size_t i = 0; i < text_len; i++) {
        ascii[i] = (bytes[i] >= 32u && bytes[i] < 127u) ? (char)bytes[i] : '.';
    }
    ascii[text_len] = '\0';
}

static void log_aupd_parser_probe(uc_engine *uc, n1g_state_t *s, uint64_t address) {
    if (address < 0x10000000u) {
        return;
    }

    static uint32_t logs;
    if (logs >= 48u) {
        return;
    }
    logs++;

    uint32_t r[6] = {0};
    uint32_t lr = 0;
    uint32_t sp = 0;
    for (int i = 0; i < 6; i++) {
        uc_reg_read(uc, arm_regs[i], &r[i]);
    }
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    uc_reg_read(uc, UC_ARM_REG_SP, &sp);

    char hex[80];
    char ascii[40];
    format_guest_bytes(uc, r[0], hex, sizeof(hex), ascii, sizeof(ascii));

    n1g_log(s,
            "aupd parser pc=0x%08llx r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x r4=0x%08x r5=0x%08x lr=0x%08x sp=0x%08x r0_words=0x%08x,0x%08x,0x%08x,0x%08x bytes=%s ascii=\"%s\"",
            (unsigned long long)address,
            r[0],
            r[1],
            r[2],
            r[3],
            r[4],
            r[5],
            lr,
            sp,
            ram_read32_or_zero(s, r[0]),
            ram_read32_or_zero(s, r[0] + 4u),
            ram_read32_or_zero(s, r[0] + 8u),
            ram_read32_or_zero(s, r[0] + 12u),
            hex,
            ascii);
}

static void log_apple_handoff_probe(uc_engine *uc, n1g_state_t *s, uint64_t address) {
    static uint32_t logs;
    if (logs >= 16u) {
        return;
    }
    logs++;

    uint32_t r[6] = {0};
    uint32_t lr = 0;
    uint32_t sp = 0;
    for (int i = 0; i < 6; i++) {
        uc_reg_read(uc, arm_regs[i], &r[i]);
    }
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    uc_reg_read(uc, UC_ARM_REG_SP, &sp);

    if (address == 0x00001388u) {
        int core = core_index_from_uc(s, uc);
        uint32_t pp_ver = n1g_dev_ppcon_read(s, 0, 4);
        uint32_t pp_selector = (pp_ver >> 16) & 0xffu;
        uint32_t cpuid = core == N1G_CORE_COP ? 0xaaaaaaaau : 0x55555555u;
        s->counters.apple_handoff_pc = (uint32_t)address;
        s->counters.apple_handoff_slot =
            (pp_selector == 0x32u || pp_selector == 0x36u) ? 0x4001ff18u : 0x40017f18u;
        s->counters.apple_handoff_seen = true;
        n1g_log(s,
                "apple handoff entry pc=0x%08llx core=%d cpuid=0x%08x ppver=0x%08x pp_selector=0x%02x expected_slot=%s",
                (unsigned long long)address,
                core,
                cpuid,
                pp_ver,
                pp_selector,
                s->counters.apple_handoff_slot == 0x4001ff18u ? "0x4001ff18" : "0x40017f18");
        return;
    }

    uint32_t handoff = r[4];
    if (address == 0x00001398u) {
        handoff = r[0];
    }
    uint32_t handoff_tag = ram_read32_or_zero(s, handoff);
    uint32_t sysinfo = ram_read32_or_zero(s, handoff + 4u);
    uint32_t sysinfo_e0 = 0;
    uint32_t sysinfo_e4 = 0;
    bool sysinfo_e0_ok = n1g_ram_read(s, sysinfo + 0xe0u, 4, &sysinfo_e0);
    bool sysinfo_e4_ok = n1g_ram_read(s, sysinfo + 0xe4u, 4, &sysinfo_e4);
    s->counters.apple_handoff_pc = (uint32_t)address;
    s->counters.apple_handoff_slot = handoff;
    s->counters.apple_handoff_tag = handoff_tag;
    s->counters.apple_handoff_sysinfo = sysinfo;
    s->counters.apple_handoff_sysinfo_e0 = sysinfo_e0;
    s->counters.apple_handoff_sysinfo_e4 = sysinfo_e4;
    s->counters.apple_handoff_seen = true;
    s->counters.apple_handoff_sysinfo_ram = sysinfo_e0_ok || sysinfo_e4_ok;

    n1g_log(s,
            "apple handoff probe pc=0x%08llx handoff=0x%08x tag=0x%08x sysinfo=0x%08x sysinfo_ram=%s sysinfo_e0=0x%08x sysinfo_e4=0x%08x r0=0x%08x r4=0x%08x lr=0x%08x sp=0x%08x words=0x%08x,0x%08x,0x%08x,0x%08x",
            (unsigned long long)address,
            handoff,
            handoff_tag,
            sysinfo,
            (sysinfo_e0_ok || sysinfo_e4_ok) ? "yes" : "no",
            sysinfo_e0,
            sysinfo_e4,
            r[0],
            r[4],
            lr,
            sp,
            ram_read32_or_zero(s, handoff),
            ram_read32_or_zero(s, handoff + 4u),
            ram_read32_or_zero(s, handoff + 8u),
            ram_read32_or_zero(s, handoff + 12u));
}

static void apple_track_progress(n1g_state_t *s, uint64_t address) {
    switch ((uint32_t)address) {
    case 0x00024c48u:
        s->counters.apple_ui_hits[0]++;
        break;
    case 0x00025024u:
        s->counters.apple_ui_hits[1]++;
        break;
    case 0x00032840u:
        s->counters.apple_ui_hits[2]++;
        break;
    case 0x00048060u:
        s->counters.apple_ui_hits[3]++;
        break;
    case 0x000480acu:
        s->counters.apple_ui_hits[4]++;
        break;
    case 0x0004ee20u:
        s->counters.apple_ui_hits[5]++;
        break;
    case 0x0004eeb4u:
        s->counters.apple_ui_hits[6]++;
        break;
    case 0x0005410cu:
        s->counters.apple_ui_hits[7]++;
        break;
    case 0x00053b18u:
        s->counters.apple_ui_hits[8]++;
        break;
    case 0x000d0bb4u:
        s->counters.apple_ui_hits[9]++;
        break;
    case 0x000d0c54u:
        s->counters.apple_ui_hits[10]++;
        break;
    case 0x00017d260u:
        s->counters.apple_ui_hits[11]++;
        break;
    case 0x00048098u:
        s->counters.apple_ui_hits[12]++;
        break;
    case 0x001c5188u:
        s->counters.apple_ui_hits[13]++;
        break;
    case 0x001c5808u:
        s->counters.apple_ui_hits[14]++;
        break;
    case 0x001c6078u:
        s->counters.apple_ui_hits[15]++;
        break;
    default:
        break;
    }

    switch ((uint32_t)address) {
    case 0x00053580u:
        s->counters.apple_lcd_task_hits[0]++;
        break;
    case 0x00053584u:
        s->counters.apple_lcd_task_hits[1]++;
        break;
    case 0x00053588u:
        s->counters.apple_lcd_task_hits[2]++;
        break;
    case 0x00053590u:
        s->counters.apple_lcd_task_hits[3]++;
        break;
    case 0x00053594u:
        s->counters.apple_lcd_task_hits[4]++;
        break;
    case 0x000535a0u:
        s->counters.apple_lcd_task_hits[5]++;
        break;
    case 0x000535a4u:
        s->counters.apple_lcd_task_hits[6]++;
        break;
    case 0x00053b04u:
        s->counters.apple_lcd_task_hits[7]++;
        break;
    case 0x00053b08u:
        s->counters.apple_lcd_task_hits[8]++;
        break;
    case 0x00053b0cu:
        s->counters.apple_lcd_task_hits[9]++;
        break;
    case 0x00053b10u:
        s->counters.apple_lcd_task_hits[10]++;
        break;
    case 0x00053b14u:
        s->counters.apple_lcd_task_hits[11]++;
        break;
    case 0x00053b18u:
        s->counters.apple_lcd_task_hits[12]++;
        break;
    case 0x00053b20u:
        s->counters.apple_lcd_task_hits[13]++;
        break;
    case 0x00053b38u:
        s->counters.apple_lcd_task_hits[14]++;
        break;
    case 0x00053db8u:
        s->counters.apple_lcd_task_hits[15]++;
        break;
    case 0x00053f28u:
        s->counters.apple_lcd_task_hits[16]++;
        break;
    case 0x00053f30u:
        s->counters.apple_lcd_task_hits[17]++;
        break;
    default:
        break;
    }

    if (address >= 0x00024d00u && address <= 0x00024effu) {
        s->counters.apple_pc_hits[0]++;
    } else if (address >= 0x0002a000u && address <= 0x0002a260u) {
        s->counters.apple_pc_hits[1]++;
    } else if (address >= 0x00048300u && address <= 0x00048400u) {
        s->counters.apple_pc_hits[2]++;
    } else if (address >= 0x00099d00u && address <= 0x00099e00u) {
        s->counters.apple_pc_hits[3]++;
    } else if (address >= 0x000c8c00u && address <= 0x000c8d00u) {
        s->counters.apple_pc_hits[4]++;
    } else if (address >= 0x00152600u && address <= 0x00152700u) {
        s->counters.apple_pc_hits[5]++;
    } else if (address >= 0x001bd000u && address <= 0x001be000u) {
        s->counters.apple_pc_hits[6]++;
    } else if (address >= 0x001c4100u && address <= 0x001c4600u) {
        s->counters.apple_pc_hits[7]++;
    } else if (address < 0x00200000u) {
        s->counters.apple_pc_hits[8]++;
    } else {
        s->counters.apple_pc_hits[9]++;
    }
}

static void hook_code(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    (void)size;
    n1g_state_t *s = (n1g_state_t *)user_data;
    if (s->opts.profile != N1G_PROFILE_APPLE) {
        return;
    }

    apple_track_progress(s, address);

    switch ((uint32_t)address) {
    case 0x00053b20u:
        record_apple_lcd_path_probe(uc, s, 0);
        break;
    case 0x00053b28u:
        record_apple_lcd_path_probe(uc, s, 1);
        break;
    case 0x00053b30u:
        record_apple_lcd_path_probe(uc, s, 2);
        break;
    case 0x00053b34u:
        record_apple_lcd_path_probe(uc, s, 3);
        break;
    case 0x00053b38u:
        record_apple_lcd_path_probe(uc, s, 4);
        break;
    case 0x000255a4u:
        record_apple_lcd_path_probe(uc, s, 5);
        break;
    case 0x000255b0u:
        record_apple_lcd_path_probe(uc, s, 6);
        break;
    case 0x000255b4u:
        record_apple_lcd_path_probe(uc, s, 7);
        break;
    case 0x00025274u:
        record_apple_lcd_path_probe(uc, s, 8);
        break;
    case 0x0004b74cu:
        record_apple_lcd_path_probe(uc, s, 9);
        break;
    case 0x00045dfcu:
        record_apple_lcd_path_probe(uc, s, 10);
        break;
    case 0x00045e6cu:
        record_apple_lcd_path_probe(uc, s, 11);
        break;
    case 0x000540a0u:
        record_apple_lcd_producer_probe(uc, s, 0);
        break;
    case 0x00054104u:
        record_apple_lcd_producer_probe(uc, s, 1);
        break;
    case 0x00054108u:
        record_apple_lcd_producer_probe(uc, s, 2);
        break;
    case 0x0005410cu:
        record_apple_lcd_producer_probe(uc, s, 3);
        break;
    case 0x00054194u:
        record_apple_lcd_producer_probe(uc, s, 4);
        break;
    case 0x000541acu:
        record_apple_lcd_producer_probe(uc, s, 5);
        break;
    case 0x000541ccu:
        record_apple_lcd_producer_probe(uc, s, 6);
        break;
    case 0x000541d4u:
        record_apple_lcd_producer_probe(uc, s, 7);
        break;
    case 0x000541dcu:
        record_apple_lcd_producer_probe(uc, s, 8);
        break;
    case 0x00054208u:
        record_apple_lcd_producer_probe(uc, s, 9);
        break;
    case 0x00054210u:
        record_apple_lcd_producer_probe(uc, s, 10);
        break;
    case 0x001c6538u:
        record_apple_input_probe(uc, s, 0);
        break;
    case 0x001c6574u:
        record_apple_input_probe(uc, s, 1);
        break;
    case 0x0002a058u:
        record_apple_input_probe(uc, s, 2);
        break;
    case 0x000b3468u:
        record_apple_input_probe(uc, s, 3);
        break;
    case 0x000b3508u:
        record_apple_input_probe(uc, s, 4);
        break;
    case 0x000d0bb4u:
        record_apple_input_probe(uc, s, 5);
        break;
    case 0x0004ee20u:
        record_apple_input_probe(uc, s, 6);
        break;
    case 0x0004ee44u:
        record_apple_input_probe(uc, s, 7);
        record_apple_ui_branch_probe(uc, s, 7);
        break;
    case 0x0004ee58u:
        record_apple_input_probe(uc, s, 8);
        break;
    case 0x0004eec8u:
        record_apple_input_probe(uc, s, 9);
        record_apple_ui_branch_probe(uc, s, 6);
        break;
    case 0x00024db4u:
        record_apple_input_probe(uc, s, 10);
        record_apple_ui_branch_probe(uc, s, 3);
        break;
    case 0x0004eeb4u:
        record_apple_input_probe(uc, s, 11);
        break;
    case 0x000483b8u:
        record_apple_ui_ready_probe(uc, s, 0);
        break;
    case 0x0004a404u:
        record_apple_ui_ready_probe(uc, s, 1);
        break;
    case 0x0004a410u:
        record_apple_ui_ready_probe(uc, s, 2);
        break;
    case 0x0004a41cu:
        record_apple_ui_ready_probe(uc, s, 3);
        break;
    case 0x0004a420u:
        record_apple_ui_ready_probe(uc, s, 4);
        break;
    case 0x001caa50u:
        record_apple_input_task_probe(uc, s, 0);
        break;
    case 0x001caa7cu:
        record_apple_input_task_probe(uc, s, 1);
        break;
    case 0x001caa84u:
        record_apple_input_task_probe(uc, s, 2);
        break;
    case 0x0002fd0cu:
        record_apple_input_task_probe(uc, s, 3);
        break;
    case 0x0002fd60u:
        record_apple_input_task_probe(uc, s, 4);
        break;
    case 0x0002fd70u:
        record_apple_input_task_probe(uc, s, 5);
        break;
    case 0x0002fd84u:
        record_apple_input_task_probe(uc, s, 6);
        break;
    case 0x0002fd9cu:
        record_apple_input_task_probe(uc, s, 7);
        break;
    case 0x0002fda8u:
        record_apple_input_task_probe(uc, s, 8);
        break;
    case 0x000b32b8u:
        record_apple_input_task_probe(uc, s, 9);
        break;
    case 0x000b32e4u:
        record_apple_input_task_probe(uc, s, 10);
        break;
    case 0x000b32ecu:
        record_apple_input_task_probe(uc, s, 11);
        break;
    case 0x000b330cu:
        record_apple_input_task_probe(uc, s, 12);
        break;
    case 0x000b310cu:
        record_apple_work_pool_probe(uc, s, 0);
        break;
    case 0x000835b4u:
        record_apple_work_pool_probe(uc, s, 1);
        break;
    case 0x000d0c54u:
        record_apple_work_pool_probe(uc, s, 2);
        break;
    case 0x00048060u:
        record_apple_input_task_probe(uc, s, 13);
        break;
    case 0x00032840u:
        record_apple_input_task_probe(uc, s, 14);
        break;
    case 0x000d0c58u:
        record_apple_work_pool_probe(uc, s, 3);
        record_apple_input_task_probe(uc, s, 15);
        break;
    case 0x000d0c5cu:
        record_apple_work_pool_probe(uc, s, 4);
        break;
    case 0x000d0c68u:
        record_apple_work_pool_probe(uc, s, 5);
        break;
    case 0x000d0c78u:
        record_apple_work_pool_probe(uc, s, 6);
        break;
    case 0x000d0c90u:
        record_apple_work_pool_probe(uc, s, 7);
        break;
    case 0x0004ec94u:
        record_apple_ui_branch_probe(uc, s, 0);
        break;
    case 0x0004eca0u:
        record_apple_ui_branch_probe(uc, s, 1);
        break;
    case 0x00025398u:
        record_apple_ui_branch_probe(uc, s, 2);
        break;
    case 0x0002539cu:
        record_apple_ui_dispatch_probe(uc, s, 0);
        break;
    case 0x000253a0u:
        record_apple_ui_dispatch_probe(uc, s, 1);
        break;
    case 0x000253a4u:
        record_apple_ui_dispatch_probe(uc, s, 2);
        break;
    case 0x0002a8b0u:
        record_apple_ui_dispatch_probe(uc, s, 3);
        break;
    case 0x0002a8d4u:
        record_apple_ui_dispatch_probe(uc, s, 4);
        break;
    case 0x0002a8ecu:
        record_apple_ui_dispatch_probe(uc, s, 5);
        break;
    case 0x0002a91cu:
        record_apple_ui_dispatch_probe(uc, s, 6);
        break;
    case 0x0002a944u:
        record_apple_ui_dispatch_probe(uc, s, 7);
        break;
    case 0x00024e20u:
        record_apple_ui_branch_probe(uc, s, 4);
        break;
    case 0x00024f08u:
        record_apple_ui_branch_probe(uc, s, 5);
        break;
    default:
        break;
    }

    if (address == 0x10001760u || address == 0x10000ee4u ||
        address == 0x10001078u || address == 0x10001afcu) {
        log_aupd_parser_probe(uc, s, address);
    }

    if (address == 0x00001388u || address == 0x00001398u ||
        address == 0x000013acu || address == 0x000013b4u) {
        log_apple_handoff_probe(uc, s, address);
    }

    if (address == 0x00032840u || address == 0x00048060u ||
        address == 0x0004ee20u || address == 0x0005410cu ||
        address == 0x000d0bb4u || address == 0x000d0c54u ||
        address == 0x00053b18u) {
        uint32_t r[6] = {0};
        uint32_t lr = 0;
        uint32_t sp = 0;
        uc_reg_read(uc, UC_ARM_REG_R0, &r[0]);
        uc_reg_read(uc, UC_ARM_REG_R1, &r[1]);
        uc_reg_read(uc, UC_ARM_REG_R2, &r[2]);
        uc_reg_read(uc, UC_ARM_REG_R3, &r[3]);
        uc_reg_read(uc, UC_ARM_REG_R4, &r[4]);
        uc_reg_read(uc, UC_ARM_REG_R5, &r[5]);
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);
        uc_reg_read(uc, UC_ARM_REG_SP, &sp);
        static uint32_t ui_anchor_logs;
        if (ui_anchor_logs < 64u) {
            ui_anchor_logs++;
            n1g_log(s,
                    "apple ui anchor pc=0x%08llx r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x r4=0x%08x r5=0x%08x lr=0x%08x sp=0x%08x r0w=0x%08x,0x%08x,0x%08x,0x%08x r4w=0x%08x,0x%08x,0x%08x,0x%08x",
                    (unsigned long long)address,
                    r[0], r[1], r[2], r[3], r[4], r[5], lr, sp,
                    ram_read32_or_zero(s, r[0]),
                    ram_read32_or_zero(s, r[0] + 4u),
                    ram_read32_or_zero(s, r[0] + 8u),
                    ram_read32_or_zero(s, r[0] + 12u),
                    ram_read32_or_zero(s, r[4]),
                    ram_read32_or_zero(s, r[4] + 4u),
                    ram_read32_or_zero(s, r[4] + 8u),
                    ram_read32_or_zero(s, r[4] + 12u));
        }
    }

    if (address == 0x0004ee20u || address == 0x0004ee44u ||
        address == 0x0004ee58u || address == 0x0004eeb4u ||
        address == 0x0004eec8u) {
        uint32_t r0 = 0;
        uint32_t r4 = 0;
        uint32_t sp = 0;
        uint32_t lr = 0;
        uc_reg_read(uc, UC_ARM_REG_R0, &r0);
        uc_reg_read(uc, UC_ARM_REG_R4, &r4);
        uc_reg_read(uc, UC_ARM_REG_SP, &sp);
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);

        uint32_t event = (address == 0x0004ee20u)
                             ? ram_read32_or_zero(s, sp + 8u)
                             : r4;
        static uint32_t lang_loop_logs;
        if (lang_loop_logs < 160u) {
            lang_loop_logs++;
            n1g_log(s,
                    "apple language loop pc=0x%08llx msg=0x%08x event=0x%08x kind=0x%08x w20=0x%08x w24=0x%08x w28=0x%08x w2c=0x%08x w30=0x%08x sp=0x%08x lr=0x%08x sp_words=0x%08x,0x%08x,0x%08x,0x%08x",
                    (unsigned long long)address,
                    r0,
                    event,
                    ram_read32_or_zero(s, event + 0x1cu),
                    ram_read32_or_zero(s, event + 0x20u),
                    ram_read32_or_zero(s, event + 0x24u),
                    ram_read32_or_zero(s, event + 0x28u),
                    ram_read32_or_zero(s, event + 0x2cu),
                    ram_read32_or_zero(s, event + 0x30u),
                    sp,
                    lr,
                    ram_read32_or_zero(s, sp),
                    ram_read32_or_zero(s, sp + 4u),
                    ram_read32_or_zero(s, sp + 8u),
                    ram_read32_or_zero(s, sp + 12u));
        }
    }

    if (address == 0x00024da4u || address == 0x00024db4u ||
        address == 0x00024e20u || address == 0x00024f08u ||
        address == 0x00024facu) {
        uint32_t r[8] = {0};
        uint32_t lr = 0;
        uint32_t sp = 0;
        for (int idx = 0; idx < 8; idx++) {
            uc_reg_read(uc, arm_regs[idx], &r[idx]);
        }
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);
        uc_reg_read(uc, UC_ARM_REG_SP, &sp);
        static uint32_t lang_handler_logs;
        if (lang_handler_logs < 96u) {
            lang_handler_logs++;
            n1g_log(s,
                    "apple language handler pc=0x%08llx r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x r4=0x%08x r5=0x%08x r6=0x%08x r7=0x%08x lr=0x%08x sp=0x%08x r0w=0x%08x,0x%08x,0x%08x,0x%08x r7w=0x%08x,0x%08x,0x%08x,0x%08x",
                    (unsigned long long)address,
                    r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], lr, sp,
                    ram_read32_or_zero(s, r[0]),
                    ram_read32_or_zero(s, r[0] + 4u),
                    ram_read32_or_zero(s, r[0] + 0x1cu),
                    ram_read32_or_zero(s, r[0] + 0x28u),
                    ram_read32_or_zero(s, r[7]),
                    ram_read32_or_zero(s, r[7] + 4u),
                    ram_read32_or_zero(s, r[7] + 0x1cu),
                    ram_read32_or_zero(s, r[7] + 0x28u));
        }
    }

    if ((address >= 0x00053580u && address <= 0x000535a4u) ||
        (address >= 0x00053b04u && address <= 0x00053b38u) ||
        address == 0x00053db8u || address == 0x00053f28u ||
        address == 0x00053f30u || address == 0x0004ec54u ||
        address == 0x000540a0u) {
        uint32_t r[6] = {0};
        uint32_t lr = 0;
        uint32_t sp = 0;
        for (int idx = 0; idx < 6; idx++) {
            uc_reg_read(uc, arm_regs[idx], &r[idx]);
        }
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);
        uc_reg_read(uc, UC_ARM_REG_SP, &sp);

        uint32_t active = ram_read32_or_zero(s, 0x10705b48u);
        uint32_t queue_from_sp = ram_read32_or_zero(s, sp + 0x390u);
        uint32_t queue_indirect = ram_read32_or_zero(s, queue_from_sp);
        uint32_t queue_slot = queue_indirect ? queue_indirect + 0x40000u : 0u;
        bool helper_path = address == 0x0004ec54u || address == 0x000540a0u;
        static uint32_t lcd_task_logs;
        static uint32_t lcd_helper_logs;
        bool should_log = false;
        if (helper_path && lcd_helper_logs < 48u) {
            lcd_helper_logs++;
            should_log = true;
        } else if (!helper_path && lcd_task_logs < 160u) {
            lcd_task_logs++;
            should_log = true;
        }
        if (should_log) {
            n1g_log(s,
                    "apple lcd task pc=0x%08llx r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x r4=0x%08x r5=0x%08x lr=0x%08x sp=0x%08x active=0x%08x active_words=0x%08x,0x%08x,0x%08x,0x%08x sp390=0x%08x qind=0x%08x qslot=0x%08x qwords=0x%08x,0x%08x,0x%08x,0x%08x",
                    (unsigned long long)address,
                    r[0], r[1], r[2], r[3], r[4], r[5], lr, sp,
                    active,
                    ram_read32_or_zero(s, active),
                    ram_read32_or_zero(s, active + 4u),
                    ram_read32_or_zero(s, active + 8u),
                    ram_read32_or_zero(s, active + 12u),
                    queue_from_sp,
                    queue_indirect,
                    queue_slot,
                    ram_read32_or_zero(s, queue_slot),
                    ram_read32_or_zero(s, queue_slot + 4u),
                    ram_read32_or_zero(s, queue_slot + 8u),
                    ram_read32_or_zero(s, queue_slot + 12u));
        }
    }

    if (address == 0x0013cc0cu || address == 0x001c86a4u ||
        address == 0x001716e0u || address == 0x00171470u ||
        address == 0x0018924cu || address == 0x00189284u ||
        address == 0x0018929cu || address == 0x00189310u ||
        address == 0x00189354u || address == 0x001894a4u ||
        address == 0x001894d0u || address == 0x00189560u ||
        address == 0x00189588u || address == 0x0018965cu ||
        address == 0x001d0900u || address == 0x001d1348u ||
        address == 0x00087310u || address == 0x0001fce4u) {
        uint32_t r[4] = {0};
        uint32_t lr = 0;
        uc_reg_read(uc, UC_ARM_REG_R0, &r[0]);
        uc_reg_read(uc, UC_ARM_REG_R1, &r[1]);
        uc_reg_read(uc, UC_ARM_REG_R2, &r[2]);
        uc_reg_read(uc, UC_ARM_REG_R3, &r[3]);
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);
        static uint32_t lcd_path_logs;
        if (lcd_path_logs < 96u) {
            lcd_path_logs++;
            n1g_log(s,
                    "apple lcd path pc=0x%08llx r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x lr=0x%08x r0w=0x%08x,0x%08x,0x%08x,0x%08x r1w=0x%08x,0x%08x,0x%08x,0x%08x",
                    (unsigned long long)address,
                    r[0], r[1], r[2], r[3], lr,
                    ram_read32_or_zero(s, r[0]),
                    ram_read32_or_zero(s, r[0] + 4u),
                    ram_read32_or_zero(s, r[0] + 8u),
                    ram_read32_or_zero(s, r[0] + 12u),
                    ram_read32_or_zero(s, r[1]),
                    ram_read32_or_zero(s, r[1] + 4u),
                    ram_read32_or_zero(s, r[1] + 8u),
                    ram_read32_or_zero(s, r[1] + 12u));
        }
        if (address == 0x00087310u) {
            uint32_t dst = r[2];
            uint32_t factory = ram_read32_or_zero(s, 0x10705ba0u);
            uint32_t handler = ram_read32_or_zero(s, dst);
            n1g_log(s,
                    "apple factory attach r0=0x%08x r1=0x%08x dst=0x%08x owner=0x%08x factory=0x%08x factory_vt=0x%08x handler=0x%08x handler_vt=0x%08x",
                    r[0],
                    r[1],
                    dst,
                    r[3],
                    factory,
                    ram_read32_or_zero(s, factory),
                    handler,
                    ram_read32_or_zero(s, handler));
        } else if (address == 0x0018924cu || address == 0x00189284u ||
                   address == 0x0018929cu || address == 0x00189310u ||
                   address == 0x00189354u || address == 0x001894a4u ||
                   address == 0x001894d0u || address == 0x00189560u ||
                   address == 0x00189588u || address == 0x0018965cu) {
            uint32_t region = r[0];
            uint32_t handler = ram_read32_or_zero(s, region + 0x44u);
            uint32_t endpoint = ram_read32_or_zero(s, region + 0x14u);
            n1g_log(s,
                    "apple lcd region pc=0x%08llx region=0x%08x endpoint=0x%08x handler=0x%08x handler_vt=0x%08x qbuf=0x%08x qhead=0x%08x qtail=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x",
                    (unsigned long long)address,
                    region,
                    endpoint,
                    handler,
                    ram_read32_or_zero(s, handler),
                    ram_read32_or_zero(s, region + 0x34u),
                    ram_read32_or_zero(s, region + 0x38u),
                    ram_read32_or_zero(s, region + 0x3cu),
                    r[1],
                    r[2],
                    r[3]);
        }
        if (address == 0x0013cc0cu) {
            uint32_t src = ram_read32_or_zero(s, r[1]);
            uint32_t nz = 0;
            uint64_t hash = 1469598103934665603ull;
            for (uint32_t offset = 0; offset < N1G_LCD_W * N1G_LCD_H * 2u; offset += 4u) {
                uint32_t word = ram_read32_or_zero(s, src + offset);
                hash ^= word;
                hash *= 1099511628211ull;
                if (word != 0) {
                    nz++;
                }
            }
            n1g_log(s,
                    "apple lcd blit source entry obj=0x%08x src=0x%08x nonzero_words=%u hash=0x%016llx rect=0x%08x words=0x%08x,0x%08x,0x%08x,0x%08x",
                    r[1],
                    src,
                    nz,
                    (unsigned long long)hash,
                    r[2],
                    ram_read32_or_zero(s, r[2]),
                    ram_read32_or_zero(s, r[2] + 4u),
                    ram_read32_or_zero(s, r[2] + 8u),
                    ram_read32_or_zero(s, r[2] + 12u));
        } else if (address == 0x0001fce4u) {
            uint32_t nz = 0;
            uint64_t hash = 1469598103934665603ull;
            for (uint32_t offset = 0; offset < N1G_LCD_W * N1G_LCD_H * 2u; offset += 4u) {
                uint32_t word = ram_read32_or_zero(s, r[0] + offset);
                hash ^= word;
                hash *= 1099511628211ull;
                if (word != 0) {
                    nz++;
                }
            }
            n1g_log(s,
                    "apple cache-prep source entry src=0x%08x arg1=0x%08x arg2=0x%08x arg3=0x%08x nonzero_words=%u hash=0x%016llx",
                    r[0],
                    r[1],
                    r[2],
                    r[3],
                    nz,
                    (unsigned long long)hash);
        }
    }

    if (address == 0x00070964u || address == 0x00077964u ||
        address == 0x00070950u || address == 0x00077970u ||
        address == 0x0002542cu || address == 0x0004b73cu ||
        address == 0x0004b740u || address == 0x000658dcu) {
        uint32_t r0 = 0;
        uint32_t r4 = 0;
        uint32_t r9 = 0;
        uint32_t lr = 0;
        uint32_t sp = 0;
        uc_reg_read(uc, UC_ARM_REG_R0, &r0);
        uc_reg_read(uc, UC_ARM_REG_R4, &r4);
        uc_reg_read(uc, UC_ARM_REG_R9, &r9);
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);
        uc_reg_read(uc, UC_ARM_REG_SP, &sp);
        static uint32_t paint_logs;
        if (paint_logs < 64u) {
            paint_logs++;
            n1g_log(s,
                    "apple paint entry pc=0x%08llx lr=0x%08x sp=0x%08x r0=0x%08x r4=0x%08x r9=0x%08x selector=0x%08x active=0x%08x active_words=0x%08x,0x%08x,0x%08x,0x%08x",
                    (unsigned long long)address,
                    lr,
                    sp,
                    r0,
                    r4,
                    r9,
                    ram_read32_or_zero(s, 0x11f9d564u),
                    ram_read32_or_zero(s, 0x10705b48u),
                    ram_read32_or_zero(s, ram_read32_or_zero(s, 0x10705b48u)),
                    ram_read32_or_zero(s, ram_read32_or_zero(s, 0x10705b48u) + 4u),
                    ram_read32_or_zero(s, ram_read32_or_zero(s, 0x10705b48u) + 8u),
                    ram_read32_or_zero(s, ram_read32_or_zero(s, 0x10705b48u) + 12u));
        }
    }

    if (address == 0x00048098u || address == 0x001c5188u ||
        address == 0x001c5808u) {
        uint32_t r[5] = {0};
        uint32_t lr = 0;
        uc_reg_read(uc, UC_ARM_REG_R0, &r[0]);
        uc_reg_read(uc, UC_ARM_REG_R1, &r[1]);
        uc_reg_read(uc, UC_ARM_REG_R4, &r[2]);
        uc_reg_read(uc, UC_ARM_REG_R5, &r[3]);
        uc_reg_read(uc, UC_ARM_REG_R8, &r[4]);
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);
        static uint32_t sched_logs;
        if (sched_logs < 80u) {
            sched_logs++;
            n1g_log(s,
                    "apple sched anchor pc=0x%08llx r0=0x%08x r1=0x%08x r4=0x%08x r5=0x%08x r8=0x%08x lr=0x%08x post_gate=0x%08x,0x%08x disp=0x%08x,0x%08x,0x%08x cur=0x%08x,0x%08x,0x%08x pkt=0x%08x,0x%08x,0x%08x,0x%08x",
                    (unsigned long long)address,
                    r[0], r[1], r[2], r[3], r[4], lr,
                    ram_read32_or_zero(s, 0x10705468u),
                    ram_read32_or_zero(s, 0x107054a4u),
                    ram_read32_or_zero(s, 0x107054b0u),
                    ram_read32_or_zero(s, 0x107054b4u),
                    ram_read32_or_zero(s, 0x107054b8u),
                    ram_read32_or_zero(s, r[3] + 12u),
                    ram_read32_or_zero(s, r[3] + 16u),
                    ram_read32_or_zero(s, r[3] + 20u),
                    ram_read32_or_zero(s, r[2]),
                    ram_read32_or_zero(s, r[2] + 4u),
                    ram_read32_or_zero(s, r[2] + 8u),
                    ram_read32_or_zero(s, r[2] + 12u));
        }
    }

    if (address == 0x000a93d8u || address == 0x000ad3f8u ||
        address == 0x000ad408u || address == 0x000d57c8u) {
        uint32_t r[4] = {0};
        uint32_t lr = 0;
        uc_reg_read(uc, UC_ARM_REG_R0, &r[0]);
        uc_reg_read(uc, UC_ARM_REG_R1, &r[1]);
        uc_reg_read(uc, UC_ARM_REG_R2, &r[2]);
        uc_reg_read(uc, UC_ARM_REG_R3, &r[3]);
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);
        static uint32_t transition_logs;
        if (transition_logs < 96u) {
            transition_logs++;
            n1g_log(s,
                    "apple transition pc=0x%08llx r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x lr=0x%08x r0w=0x%08x,0x%08x,0x%08x,0x%08x r2w=0x%08x,0x%08x,0x%08x,0x%08x cbslots=0x%08x,0x%08x",
                    (unsigned long long)address,
                    r[0],
                    r[1],
                    r[2],
                    r[3],
                    lr,
                    ram_read32_or_zero(s, r[0]),
                    ram_read32_or_zero(s, r[0] + 4u),
                    ram_read32_or_zero(s, r[0] + 8u),
                    ram_read32_or_zero(s, r[0] + 12u),
                    ram_read32_or_zero(s, r[2]),
                    ram_read32_or_zero(s, r[2] + 4u),
                    ram_read32_or_zero(s, r[2] + 8u),
                    ram_read32_or_zero(s, r[2] + 12u),
                    ram_read32_or_zero(s, 0x10709558u),
                    ram_read32_or_zero(s, 0x1070955cu));
        }
    }

    if (address == 0x0000c9a4u) {
        uint32_t blk = 0;
        uint32_t words[10] = {0};
        uc_reg_read(uc, UC_ARM_REG_R0, &blk);
        for (uint32_t i = 0; i < 10u; i++) {
            words[i] = ram_read32_or_zero(s, blk + i * 4u);
        }
        uint32_t active = ram_read32_or_zero(s, 0x10705b48u);
        uint32_t active0 = ram_read32_or_zero(s, active);
        uint32_t active4 = ram_read32_or_zero(s, active + 4u);
        uint32_t active8 = ram_read32_or_zero(s, active + 8u);
        uint32_t active12 = ram_read32_or_zero(s, active + 12u);

        static uint32_t kcall_logs;
        static uint32_t lcd_kcall_logs;
        bool interesting_op =
            words[0] == 1u || words[0] == 2u || words[0] == 3u ||
            words[0] == 5u || words[0] == 6u || words[0] == 7u ||
            words[0] == 12u || words[0] == 13u || words[0] == 14u ||
            words[0] == 15u || words[0] == 18u || words[0] == 19u ||
            words[0] == 20u || words[0] == 24u || words[0] == 25u ||
            words[0] == 45u;
        bool late_lcd = active != 0u && interesting_op && lcd_kcall_logs < 256u;
        bool early_log = kcall_logs < 96u && interesting_op && active == 0u;
        if (early_log || late_lcd) {
            uint32_t lr = 0;
            uc_reg_read(uc, UC_ARM_REG_LR, &lr);
            if (late_lcd) {
                lcd_kcall_logs++;
            } else {
                kcall_logs++;
            }
            n1g_log(s,
                    "apple kcall pc=0x%08llx op=0x%08x blk=0x%08x lr=0x%08x words=0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x active=0x%08x active_words=0x%08x,0x%08x,0x%08x,0x%08x",
                    (unsigned long long)address,
                    words[0],
                    blk,
                    lr,
                    words[1],
                    words[2],
                    words[3],
                    words[4],
                    words[5],
                    words[6],
                    words[7],
                    words[8],
                    words[9],
                    active,
                    active0,
                    active4,
                    active8,
                    active12);
        }
        return;
    }

    if (address != 0x000895acu) {
        return;
    }

    uint32_t r0 = 0;
    uint32_t r4 = 0;
    uint32_t lr = 0;
    uc_reg_read(uc, UC_ARM_REG_R0, &r0);
    uc_reg_read(uc, UC_ARM_REG_R4, &r4);
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    if ((r0 & 0xff000000u) == 0u) {
        return;
    }

    static uint32_t logs;
    if (logs >= 16u) {
        return;
    }
    logs++;

    uint32_t ptr8 = ram_read32_or_zero(s, r4 + 8u);
    n1g_log(s,
            "apple caller bad object id pc=0x%08llx r0=0x%08x r4=0x%08x lr=0x%08x obj0=0x%08x obj4=0x%08x obj8=0x%08x ptr8_0=0x%08x",
            (unsigned long long)address,
            r0,
            r4,
            lr,
            ram_read32_or_zero(s, r4),
            ram_read32_or_zero(s, r4 + 4u),
            ptr8,
            ram_read32_or_zero(s, ptr8));
}

static void hook_apple_key_gate_write(uc_engine *uc,
                                      uc_mem_type type,
                                      uint64_t address,
                                      int size,
                                      int64_t value,
                                      void *user_data) {
    (void)type;
    n1g_state_t *s = (n1g_state_t *)user_data;
    if (s->opts.profile != N1G_PROFILE_APPLE) {
        return;
    }

    uint32_t pc = 0;
    uint32_t lr = 0;
    uint32_t r0 = 0;
    uint32_t r1 = 0;
    uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    uc_reg_read(uc, UC_ARM_REG_LR, &lr);
    uc_reg_read(uc, UC_ARM_REG_R0, &r0);
    uc_reg_read(uc, UC_ARM_REG_R1, &r1);

    s->counters.apple_key_gate_writes++;
    s->counters.apple_key_gate_last[0] = pc;
    s->counters.apple_key_gate_last[1] = (uint32_t)address;
    s->counters.apple_key_gate_last[2] = (uint32_t)size;
    s->counters.apple_key_gate_last[3] = (uint32_t)value;
    s->counters.apple_key_gate_last[4] = r0;
    s->counters.apple_key_gate_last[5] = r1;
    s->counters.apple_key_gate_last[6] = lr;
    s->counters.apple_key_gate_last[7] = ram_read32_or_zero(s, 0x10705468u);
    s->counters.apple_key_gate_bytes = ram_read32_or_zero(s, 0x1070546cu);
    if (s->counters.apple_key_gate_writes <= 32u) {
        n1g_info(s,
                 "apple key-gate write #%llu pc=0x%08x addr=0x%08x size=%d value=0x%08x r0=0x%08x r1=0x%08x lr=0x%08x pre68=0x%08x pre6c=0x%08x",
                 (unsigned long long)s->counters.apple_key_gate_writes,
                 pc,
                 (uint32_t)address,
                 size,
                 (uint32_t)value,
                 r0,
                 r1,
                 lr,
                 s->counters.apple_key_gate_last[7],
                 s->counters.apple_key_gate_bytes);
    }
}

static bool hook_mem_invalid(uc_engine *uc,
                             uc_mem_type type,
                             uint64_t address,
                             int size,
                             int64_t value,
                             void *user_data) {
    n1g_state_t *s = (n1g_state_t *)user_data;
    uint32_t pc = 0;
    uint32_t cpsr = 0;
    uint32_t regs[10] = {0};
    uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    uc_reg_read(uc, UC_ARM_REG_CPSR, &cpsr);
    uc_reg_read(uc, UC_ARM_REG_R0, &regs[0]);
    uc_reg_read(uc, UC_ARM_REG_R1, &regs[1]);
    uc_reg_read(uc, UC_ARM_REG_R2, &regs[2]);
    uc_reg_read(uc, UC_ARM_REG_R3, &regs[3]);
    uc_reg_read(uc, UC_ARM_REG_R4, &regs[4]);
    uc_reg_read(uc, UC_ARM_REG_R5, &regs[5]);
    uc_reg_read(uc, UC_ARM_REG_R6, &regs[6]);
    uc_reg_read(uc, UC_ARM_REG_R7, &regs[7]);
    uc_reg_read(uc, UC_ARM_REG_SP, &regs[8]);
    uc_reg_read(uc, UC_ARM_REG_LR, &regs[9]);
    n1g_log(s,
            "invalid memory type=%d pc=0x%08x addr=0x%08llx size=%d value=0x%08llx r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x r4=0x%08x r5=0x%08x r6=0x%08x r7=0x%08x sp=0x%08x lr=0x%08x",
            (int)type,
            pc,
            (unsigned long long)address,
            size,
            (unsigned long long)value,
            regs[0],
            regs[1],
            regs[2],
            regs[3],
            regs[4],
            regs[5],
            regs[6],
            regs[7],
            regs[8],
            regs[9]);
    n1g_log(s, "invalid cpsr=0x%08x", cpsr);
    if (s->opts.profile == N1G_PROFILE_APPLE) {
        n1g_log(s,
                "apple invalid context r0_words=0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x r4_words=0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x",
                ram_read32_or_zero(s, regs[0] + 0u),
                ram_read32_or_zero(s, regs[0] + 4u),
                ram_read32_or_zero(s, regs[0] + 8u),
                ram_read32_or_zero(s, regs[0] + 12u),
                ram_read32_or_zero(s, regs[0] + 16u),
                ram_read32_or_zero(s, regs[0] + 20u),
                ram_read32_or_zero(s, regs[0] + 24u),
                ram_read32_or_zero(s, regs[0] + 28u),
                ram_read32_or_zero(s, regs[4] + 0u),
                ram_read32_or_zero(s, regs[4] + 4u),
                ram_read32_or_zero(s, regs[4] + 8u),
                ram_read32_or_zero(s, regs[4] + 12u),
                ram_read32_or_zero(s, regs[4] + 16u),
                ram_read32_or_zero(s, regs[4] + 20u),
                ram_read32_or_zero(s, regs[4] + 24u),
                ram_read32_or_zero(s, regs[4] + 28u));
    }
    return false;
}

static const char *flash_command_name(uint32_t value) {
    switch (value & 0xffu) {
    case 0x10u:
        return "program-alt";
    case 0x20u:
        return "erase-setup";
    case 0x40u:
        return "program";
    case 0x50u:
        return "clear-status";
    case 0x70u:
        return "read-status";
    case 0x90u:
        return "read-id";
    case 0x98u:
        return "cfi-query";
    case 0xd0u:
        return "erase-confirm";
    case 0xf0u:
    case 0xffu:
        return "read-array";
    default:
        return NULL;
    }
}

static uint32_t low0_backing_read(n1g_state_t *s, uint32_t addr, int size) {
    if (size <= 0 || size > 4 || addr >= N1G_FLASH_SIZE) {
        return 0;
    }
    if (s->low0_map == N1G_LOW0_FLASH) {
        return n1g_dev_flash_read(s, addr, (uint32_t)size);
    }

    uint32_t v = 0;
    for (int i = 0; i < size && addr + (uint32_t)i < N1G_SDRAM_SIZE; i++) {
        v |= ((uint32_t)s->ram.sdram[addr + (uint32_t)i]) << (8u * (uint32_t)i);
    }
    return v;
}

static void hook_mem_read(uc_engine *uc,
                          uc_mem_type type,
                          uint64_t address,
                          int size,
                          int64_t value,
                          void *user_data) {
    (void)type;
    (void)value;
    n1g_state_t *s = (n1g_state_t *)user_data;
    if (s->opts.profile != N1G_PROFILE_APPLE) {
        return;
    }

    static uint32_t low0_read_logs;
    uint32_t addr = (uint32_t)address;
    if (addr >= N1G_FLASH_SIZE || low0_read_logs >= 96u) {
        return;
    }

    uint32_t pc = 0;
    uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    uint32_t read_value = low0_backing_read(s, addr, size);
    low0_read_logs++;
    n1g_log(s,
            "apple low0 read pc=0x%08x addr=0x%08x size=%d value=0x%08x low0_map=%d flash_mode=%u",
            pc,
            addr,
            size,
            read_value,
            (int)s->low0_map,
            (unsigned)s->flash.mode);
}

static void hook_mem_write(uc_engine *uc,
                           uc_mem_type type,
                           uint64_t address,
                           int size,
                           int64_t value,
                           void *user_data) {
    (void)type;
    n1g_state_t *s = (n1g_state_t *)user_data;
    if (s->opts.profile != N1G_PROFILE_APPLE) {
        return;
    }

    static uint64_t high_writes;
    static uint64_t high_nonzero_writes;
    static uint64_t active_src_writes;
    static uint64_t dense_src_writes;
    static uint32_t write_logs;
    static uint32_t frame_write_logs;
    static uint32_t active_src_logs;
    static uint32_t dense_src_logs;
    static uint32_t ui_target_logs;
    static uint32_t ui_event_logs;
    static uint32_t ui_desc_logs;
    static uint32_t ui_lcd_global_logs;
    static uint32_t ui_global_logs;
    static uint32_t ui_object_logs;
    static uint32_t ui_stack_logs;
    static uint32_t low0_logs;
    static uint32_t low0_cmd_logs;
    uint32_t addr = (uint32_t)address;
    uint32_t val = (uint32_t)value;

    if (addr < 0x10706150u && addr + (uint32_t)size > 0x1070613cu) {
        static uint32_t accessory_state_logs;
        if (accessory_state_logs < 128u) {
            uint32_t pc = 0;
            uc_reg_read(uc, UC_ARM_REG_PC, &pc);
            accessory_state_logs++;
            n1g_log(s,
                    "apple accessory-state write pc=0x%08x addr=0x%08x size=%d value=0x%08x count=%u",
                    pc, addr, size, val, accessory_state_logs);
        }
    }

    if (addr < N1G_FLASH_SIZE) {
        const char *cmd = flash_command_name(val);
        bool should_log = false;
        if (cmd && low0_cmd_logs < 96u) {
            low0_cmd_logs++;
            should_log = true;
        } else if (low0_logs < 32u) {
            low0_logs++;
            should_log = true;
        }
        if (should_log) {
            uint32_t pc = 0;
            uc_reg_read(uc, UC_ARM_REG_PC, &pc);
            n1g_log(s,
                    "apple low0 write pc=0x%08x addr=0x%08x size=%d value=0x%08x flash_cmd=%s low0_map=%d",
                    pc,
                    addr,
                    size,
                    val,
                    cmd ? cmd : "-",
                    (int)s->low0_map);
        }
        return;
    }

    high_writes++;
    if (val != 0) {
        high_nonzero_writes++;
    }

    if (addr >= 0x107333b0u && addr < 0x10733440u && ui_event_logs < 256u) {
        uint32_t pc = 0;
        uc_reg_read(uc, UC_ARM_REG_PC, &pc);
        ui_event_logs++;
        n1g_log(s,
                "apple ui-event write pc=0x%08x addr=0x%08x size=%d value=0x%08x count=%u",
                pc,
                addr,
                size,
                val,
                ui_event_logs);
    }

    if (addr >= 0x107433d0u && addr < 0x10743540u && ui_desc_logs < 384u) {
        uint32_t pc = 0;
        uc_reg_read(uc, UC_ARM_REG_PC, &pc);
        ui_desc_logs++;
        n1g_log(s,
                "apple ui-desc write pc=0x%08x addr=0x%08x size=%d value=0x%08x count=%u",
                pc,
                addr,
                size,
                val,
                ui_desc_logs);
    }

    if (addr >= 0x10705b20u && addr < 0x10705b80u && ui_lcd_global_logs < 256u) {
        uint32_t pc = 0;
        uc_reg_read(uc, UC_ARM_REG_PC, &pc);
        ui_lcd_global_logs++;
        n1g_log(s,
                "apple ui-lcd-global write pc=0x%08x addr=0x%08x size=%d value=0x%08x count=%u",
                pc,
                addr,
                size,
                val,
                ui_lcd_global_logs);
    }

    if (((addr >= 0x107333b0u && addr < 0x10733440u) ||
         (addr >= 0x107433d0u && addr < 0x10743540u) ||
         (addr >= 0x10705b20u && addr < 0x10705b80u) ||
         (addr >= 0x10705460u && addr < 0x107054c0u && addr != 0x107054a5u)) &&
        ui_target_logs < 512u) {
        uint32_t pc = 0;
        uc_reg_read(uc, UC_ARM_REG_PC, &pc);
        ui_target_logs++;
        n1g_log(s,
                "apple ui-target write pc=0x%08x addr=0x%08x size=%d value=0x%08x count=%u",
                pc,
                addr,
                size,
                val,
                ui_target_logs);
    }

    if (addr >= 0x10705000u && addr < 0x10706000u && ui_global_logs < 160u) {
        uint32_t pc = 0;
        uc_reg_read(uc, UC_ARM_REG_PC, &pc);
        ui_global_logs++;
        n1g_log(s,
                "apple ui-global write pc=0x%08x addr=0x%08x size=%d value=0x%08x count=%u",
                pc,
                addr,
                size,
                val,
                ui_global_logs);
    }

    if (addr >= 0x10732000u && addr < 0x10744000u && ui_object_logs < 320u) {
        uint32_t pc = 0;
        uc_reg_read(uc, UC_ARM_REG_PC, &pc);
        ui_object_logs++;
        n1g_log(s,
                "apple ui-object write pc=0x%08x addr=0x%08x size=%d value=0x%08x count=%u",
                pc,
                addr,
                size,
                val,
                ui_object_logs);
    }

    if (addr >= 0x107a0000u && addr < 0x107b0000u && ui_stack_logs < 96u) {
        uint32_t pc = 0;
        uc_reg_read(uc, UC_ARM_REG_PC, &pc);
        ui_stack_logs++;
        n1g_log(s,
                "apple ui-stack write pc=0x%08x addr=0x%08x size=%d value=0x%08x count=%u",
                pc,
                addr,
                size,
                val,
                ui_stack_logs);
    }

    if (addr >= 0x11fe89e0u && addr < 0x11ff3f60u) {
        active_src_writes++;
        if (active_src_logs < 48u) {
            uint32_t pc = 0;
            uc_reg_read(uc, UC_ARM_REG_PC, &pc);
            active_src_logs++;
            n1g_log(s,
                    "apple active-src write pc=0x%08x addr=0x%08x size=%d value=0x%08x count=%llu",
                    pc,
                    addr,
                    size,
                    val,
                    (unsigned long long)active_src_writes);
        }
    }

    if (addr >= 0x11fbb000u && addr < 0x11fc6560u) {
        dense_src_writes++;
        if (dense_src_logs < 48u) {
            uint32_t pc = 0;
            uc_reg_read(uc, UC_ARM_REG_PC, &pc);
            dense_src_logs++;
            n1g_log(s,
                    "apple dense-src write pc=0x%08x addr=0x%08x size=%d value=0x%08x count=%llu",
                    pc,
                    addr,
                    size,
                    val,
                    (unsigned long long)dense_src_writes);
        }
    }

    if (frame_write_logs < 64u && addr >= 0x11ff44f0u && addr < 0x12000000u) {
        uint32_t pc = 0;
        uc_reg_read(uc, UC_ARM_REG_PC, &pc);
        frame_write_logs++;
        n1g_log(s,
                "apple frame-source write pc=0x%08x addr=0x%08x size=%d value=0x%08x total=%llu nonzero=%llu",
                pc,
                addr,
                size,
                val,
                (unsigned long long)high_writes,
                (unsigned long long)high_nonzero_writes);
    }

    if (write_logs < 64u && val != 0 &&
        (addr >= 0x11fe0000u || (addr >= 0x11fa0000u && addr < 0x11fc0000u))) {
        uint32_t pc = 0;
        uc_reg_read(uc, UC_ARM_REG_PC, &pc);
        write_logs++;
        n1g_log(s,
                "apple high ram write pc=0x%08x addr=0x%08x size=%d value=0x%08x total=%llu nonzero=%llu",
                pc,
                addr,
                size,
                val,
                (unsigned long long)high_writes,
                (unsigned long long)high_nonzero_writes);
    }
}

static void hook_flash_write(uc_engine *uc,
                             uc_mem_type type,
                             uint64_t address,
                             int size,
                             int64_t value,
                             void *user_data) {
    (void)type;
    n1g_state_t *s = (n1g_state_t *)user_data;
    uint64_t offset = address;
    if (address >= N1G_FLASH_ALIAS_BASE && address < N1G_FLASH_ALIAS_BASE + N1G_FLASH_SIZE) {
        offset = address - N1G_FLASH_ALIAS_BASE;
    } else if (address < N1G_FLASH_SIZE && s->low0_map != N1G_LOW0_FLASH) {
        return;
    }
    if (offset >= N1G_FLASH_SIZE || size <= 0 || size > 4) {
        return;
    }

    n1g_dev_flash_write(s, (uint32_t)offset, (uint32_t)size, (uint32_t)value);

    /* Keep fallback Unicorn maps coherent; map_ptr users see s->flash.bytes. */
    if (s->low0_map == N1G_LOW0_FLASH) {
        (void)uc_mem_write(uc, N1G_FLASH_BASE, s->flash.bytes, 0x100u);
    }
    if (s->flash_alias_mapped) {
        (void)uc_mem_write(uc, N1G_FLASH_ALIAS_BASE, s->flash.bytes, 0x100u);
    }
}

static bool map_ram_one(n1g_state_t *s,
                        uc_engine *uc,
                        uint64_t addr,
                        size_t size,
                        void *ptr,
                        const char *name) {
    uc_err err = uc_mem_map_ptr(uc, addr, size, UC_PROT_ALL, ptr);
    if (err == UC_ERR_OK) {
        n1g_log(s, "ram map ptr %s addr=0x%08llx size=0x%zx",
                name, (unsigned long long)addr, size);
        return true;
    }
    n1g_log(s, "ram map ptr fallback %s addr=0x%08llx size=0x%zx: %s",
            name, (unsigned long long)addr, size, uc_strerror(err));
    err = uc_mem_map(uc, addr, size, UC_PROT_ALL);
    if (err != UC_ERR_OK) {
        n1g_log(s, "ram map fallback failed %s addr=0x%08llx size=0x%zx: %s",
                name, (unsigned long long)addr, size, uc_strerror(err));
        return false;
    }
    return uc_mem_write(uc, addr, ptr, size) == UC_ERR_OK;
}

static bool map_flash_one_at(n1g_state_t *s, uc_engine *uc, uint64_t addr, const char *name) {
    uc_err err = uc_mem_map_ptr(uc,
                                addr,
                                N1G_FLASH_SIZE,
                                UC_PROT_ALL,
                                s->flash.bytes);
    if (err == UC_ERR_OK) {
        n1g_log(s, "flash map ptr %s addr=0x%08llx size=0x%x",
                name, (unsigned long long)addr, N1G_FLASH_SIZE);
        return true;
    }

    n1g_log(s, "flash map ptr fallback %s addr=0x%08llx size=0x%x: %s",
            name,
            (unsigned long long)addr,
            N1G_FLASH_SIZE,
            uc_strerror(err));
    err = uc_mem_map(uc, addr, N1G_FLASH_SIZE, UC_PROT_ALL);
    if (err != UC_ERR_OK) {
        n1g_log(s, "flash map fallback failed %s addr=0x%08llx size=0x%x: %s",
                name,
                (unsigned long long)addr,
                N1G_FLASH_SIZE,
                uc_strerror(err));
        return false;
    }
    return uc_mem_write(uc, addr, s->flash.bytes, N1G_FLASH_SIZE) == UC_ERR_OK;
}

static bool map_flash_one(n1g_state_t *s, uc_engine *uc) {
    return map_flash_one_at(s, uc, N1G_FLASH_BASE, "low0");
}

static bool map_mmio(n1g_state_t *s, uc_engine *uc, n1g_core_t core, uint32_t base, size_t size) {
    if (s->mmio_context_count >= sizeof(s->mmio_contexts) / sizeof(s->mmio_contexts[0])) {
        return false;
    }
    mmio_ctx_t *ctx = (mmio_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        return false;
    }
    ctx->s = s;
    ctx->base = base;
    ctx->core = core;
    s->mmio_contexts[s->mmio_context_count++] = ctx;
    uc_err err = uc_mmio_map(uc, base, size, mmio_read_cb, ctx, mmio_write_cb, ctx);
    if (err != UC_ERR_OK) {
        n1g_log(s, "uc_mmio_map failed base=0x%08x size=0x%zx: %s", base, size, uc_strerror(err));
        return false;
    }
    return true;
}

static bool add_hook_checked(n1g_state_t *s,
                             uc_engine *uc,
                             uc_hook_type type,
                             void *callback,
                             uint64_t begin,
                             uint64_t end) {
    uc_hook hook;
    uc_err err = uc_hook_add(uc, &hook, type, callback, s, begin, end);
    if (err != UC_ERR_OK) {
        n1g_info(s, "uc_hook_add failed type=%d begin=0x%08llx end=0x%08llx: %s",
                 (int)type,
                 (unsigned long long)begin,
                 (unsigned long long)end,
                 uc_strerror(err));
        return false;
    }
    return true;
}

static bool add_code_hook(n1g_state_t *s, uc_engine *uc, uint32_t begin, uint32_t end) {
    return add_hook_checked(s, uc, UC_HOOK_CODE, (void *)hook_code, begin, end);
}

/* Generic --probe-pc hook: log registers whenever execution reaches a
 * requested address. Profile-independent debugging aid. */
static void hook_probe_pc(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    (void)size;
    n1g_state_t *s = (n1g_state_t *)user_data;
    uint32_t r[7] = {0};
    static const int regs[7] = {UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                                UC_ARM_REG_R3, UC_ARM_REG_R12, UC_ARM_REG_LR,
                                UC_ARM_REG_SP};
    for (int i = 0; i < 7; i++) {
        uc_reg_read(uc, regs[i], &r[i]);
    }
    n1g_info(s,
             "probe pc=0x%08x r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x r12=0x%08x lr=0x%08x sp=0x%08x ticks=%llu",
             (uint32_t)address,
             r[0], r[1], r[2], r[3], r[4], r[5], r[6],
             (unsigned long long)s->counters.device_ticks);
}

static bool add_probe_pc_hooks(n1g_state_t *s, uc_engine *uc) {
    for (uint32_t i = 0; i < s->opts.probe_pc_count; i++) {
        if (!add_hook_checked(s, uc, UC_HOOK_CODE, (void *)hook_probe_pc,
                              s->opts.probe_pc[i], s->opts.probe_pc[i] + 3u)) {
            return false;
        }
    }
    return true;
}

static bool add_apple_progress_hooks(n1g_state_t *s, uc_engine *uc) {
    return add_code_hook(s, uc, 0x00001388u, 0x000013b8u) &&
           add_code_hook(s, uc, 0x0000c9a4u, 0x0000c9a8u) &&
           add_code_hook(s, uc, 0x0000c9f4u, 0x0000c9f8u) &&
           add_code_hook(s, uc, 0x00024c48u, 0x00024c4cu) &&
           add_code_hook(s, uc, 0x00025024u, 0x00025028u) &&
           add_code_hook(s, uc, 0x00025398u, 0x000253a8u) &&
           add_code_hook(s, uc, 0x0002a8b0u, 0x0002a948u) &&
           add_code_hook(s, uc, 0x00032840u, 0x00032844u) &&
           add_code_hook(s, uc, 0x0002a058u, 0x0002a05cu) &&
           add_code_hook(s, uc, 0x0002fd0cu, 0x0002fd10u) &&
           add_code_hook(s, uc, 0x0002fd60u, 0x0002fd64u) &&
           add_code_hook(s, uc, 0x0002fd70u, 0x0002fd74u) &&
           add_code_hook(s, uc, 0x0002fd84u, 0x0002fd88u) &&
           add_code_hook(s, uc, 0x0002fd9cu, 0x0002fda0u) &&
           add_code_hook(s, uc, 0x0002fda8u, 0x0002fdacu) &&
           add_code_hook(s, uc, 0x000835b4u, 0x000835b8u) &&
           add_code_hook(s, uc, 0x000b310cu, 0x000b3110u) &&
           add_code_hook(s, uc, 0x000b32b8u, 0x000b32bcu) &&
           add_code_hook(s, uc, 0x000b32e4u, 0x000b32f0u) &&
           add_code_hook(s, uc, 0x000b330cu, 0x000b3310u) &&
           add_code_hook(s, uc, 0x000b3468u, 0x000b346cu) &&
           add_code_hook(s, uc, 0x000b3508u, 0x000b350cu) &&
           add_code_hook(s, uc, 0x00048060u, 0x00048064u) &&
           add_code_hook(s, uc, 0x000483b8u, 0x000483bcu) &&
           add_code_hook(s, uc, 0x00048098u, 0x0004809cu) &&
           add_code_hook(s, uc, 0x000480acu, 0x000480b0u) &&
           add_code_hook(s, uc, 0x0004a404u, 0x0004a424u) &&
           add_code_hook(s, uc, 0x00045dfcu, 0x00045e00u) &&
           add_code_hook(s, uc, 0x00045e6cu, 0x00045e70u) &&
           add_code_hook(s, uc, 0x0004b74cu, 0x0004b750u) &&
           add_code_hook(s, uc, 0x0004ec94u, 0x0004eca4u) &&
           add_code_hook(s, uc, 0x0004ee20u, 0x0004ee24u) &&
           add_code_hook(s, uc, 0x0004ee44u, 0x0004ee48u) &&
           add_code_hook(s, uc, 0x0004ee58u, 0x0004ee5cu) &&
           add_code_hook(s, uc, 0x0004eeb4u, 0x0004eeb8u) &&
           add_code_hook(s, uc, 0x0004eec8u, 0x0004eeccu) &&
           add_code_hook(s, uc, 0x00024db4u, 0x00024db8u) &&
           add_code_hook(s, uc, 0x00024e20u, 0x00024e24u) &&
           add_code_hook(s, uc, 0x00024f08u, 0x00024f0cu) &&
           add_code_hook(s, uc, 0x000540a0u, 0x00054214u) &&
           add_code_hook(s, uc, 0x00025274u, 0x00025278u) &&
           add_code_hook(s, uc, 0x000255a4u, 0x000255b8u) &&
           add_code_hook(s, uc, 0x00053b18u, 0x00053b1cu) &&
           add_code_hook(s, uc, 0x00053b20u, 0x00053b3cu) &&
           add_code_hook(s, uc, 0x000d0bb4u, 0x000d0bb8u) &&
           add_code_hook(s, uc, 0x000d0c54u, 0x000d0c58u) &&
           add_code_hook(s, uc, 0x000d0c58u, 0x000d0c5cu) &&
           add_code_hook(s, uc, 0x000d0c5cu, 0x000d0c60u) &&
           add_code_hook(s, uc, 0x000d0c68u, 0x000d0c6cu) &&
           add_code_hook(s, uc, 0x000d0c78u, 0x000d0c7cu) &&
           add_code_hook(s, uc, 0x000d0c90u, 0x000d0c94u) &&
           add_code_hook(s, uc, 0x0017d260u, 0x0017d264u) &&
           add_code_hook(s, uc, 0x001c5188u, 0x001c518cu) &&
           add_code_hook(s, uc, 0x001c5808u, 0x001c580cu) &&
           add_code_hook(s, uc, 0x001c6078u, 0x001c607cu) &&
           add_code_hook(s, uc, 0x001c6538u, 0x001c653cu) &&
           add_code_hook(s, uc, 0x001c6574u, 0x001c6578u) &&
           add_code_hook(s, uc, 0x001caa50u, 0x001caa54u) &&
           add_code_hook(s, uc, 0x001caa7cu, 0x001caa88u);
}

static bool add_apple_verbose_hooks(n1g_state_t *s, uc_engine *uc) {
    return add_code_hook(s, uc, 0x0000c9a4u, 0x0000c9a8u) &&
           add_code_hook(s, uc, 0x0000c9f4u, 0x0000c9f8u) &&
           add_code_hook(s, uc, 0x0001fce4u, 0x0001fce8u) &&
           add_code_hook(s, uc, 0x00001388u, 0x000013b8u) &&
           add_code_hook(s, uc, 0x00024d00u, 0x00024effu) &&
           add_code_hook(s, uc, 0x00024c40u, 0x00025030u) &&
           add_code_hook(s, uc, 0x00025398u, 0x000253a8u) &&
           add_code_hook(s, uc, 0x0002a058u, 0x0002a05cu) &&
           add_code_hook(s, uc, 0x0002a8b0u, 0x0002a948u) &&
           add_code_hook(s, uc, 0x0002fd0cu, 0x0002fdacu) &&
           add_code_hook(s, uc, 0x000835b4u, 0x000835b8u) &&
           add_code_hook(s, uc, 0x00025274u, 0x00025278u) &&
           add_code_hook(s, uc, 0x000255a4u, 0x000255b8u) &&
           add_code_hook(s, uc, 0x00032840u, 0x00032844u) &&
           add_code_hook(s, uc, 0x000b310cu, 0x000b3110u) &&
           add_code_hook(s, uc, 0x000b32b8u, 0x000b3310u) &&
           add_code_hook(s, uc, 0x000b3468u, 0x000b346cu) &&
           add_code_hook(s, uc, 0x000b3508u, 0x000b350cu) &&
           add_code_hook(s, uc, 0x00045dfcu, 0x00045e00u) &&
           add_code_hook(s, uc, 0x00045e6cu, 0x00045e70u) &&
           add_code_hook(s, uc, 0x0004b74cu, 0x0004b750u) &&
           add_code_hook(s, uc, 0x00048060u, 0x000480b0u) &&
           add_code_hook(s, uc, 0x000483b8u, 0x000483bcu) &&
           add_code_hook(s, uc, 0x00048300u, 0x00048400u) &&
           add_code_hook(s, uc, 0x0004a404u, 0x0004a424u) &&
           add_code_hook(s, uc, 0x0004ec54u, 0x0004ec58u) &&
           add_code_hook(s, uc, 0x0004ee20u, 0x0004eec8u) &&
           add_code_hook(s, uc, 0x00053580u, 0x000535a8u) &&
           add_code_hook(s, uc, 0x00053b04u, 0x00053b40u) &&
           add_code_hook(s, uc, 0x00053db8u, 0x00053dbcu) &&
           add_code_hook(s, uc, 0x00053f28u, 0x00053f34u) &&
           add_code_hook(s, uc, 0x000540a0u, 0x00054214u) &&
           add_code_hook(s, uc, 0x00087310u, 0x00087314u) &&
           add_code_hook(s, uc, 0x000a93d8u, 0x000a9400u) &&
           add_code_hook(s, uc, 0x000ad3f8u, 0x000ad430u) &&
           add_code_hook(s, uc, 0x00099d00u, 0x00099e00u) &&
           add_code_hook(s, uc, 0x000d0bb4u, 0x000d0c58u) &&
           add_code_hook(s, uc, 0x000d0c58u, 0x000d0c5cu) &&
           add_code_hook(s, uc, 0x000d0c5cu, 0x000d0c94u) &&
           add_code_hook(s, uc, 0x000d57c8u, 0x000d5800u) &&
           add_code_hook(s, uc, 0x0013cc0cu, 0x0013cc10u) &&
           add_code_hook(s, uc, 0x00152600u, 0x00152700u) &&
           add_code_hook(s, uc, 0x00171470u, 0x00171474u) &&
           add_code_hook(s, uc, 0x001716e0u, 0x001716e4u) &&
           add_code_hook(s, uc, 0x0017d260u, 0x0017d264u) &&
           add_code_hook(s, uc, 0x0018924cu, 0x00189250u) &&
           add_code_hook(s, uc, 0x00189284u, 0x001892a0u) &&
           add_code_hook(s, uc, 0x00189310u, 0x00189314u) &&
           add_code_hook(s, uc, 0x00189354u, 0x00189358u) &&
           add_code_hook(s, uc, 0x001894a4u, 0x001894d4u) &&
           add_code_hook(s, uc, 0x00189560u, 0x0018958cu) &&
           add_code_hook(s, uc, 0x0018965cu, 0x00189660u) &&
           add_code_hook(s, uc, 0x001c5188u, 0x001c5190u) &&
           add_code_hook(s, uc, 0x001c5808u, 0x001c5810u) &&
           add_code_hook(s, uc, 0x001c6078u, 0x001c607cu) &&
           add_code_hook(s, uc, 0x001c6538u, 0x001c653cu) &&
           add_code_hook(s, uc, 0x001c6574u, 0x001c6578u) &&
           add_code_hook(s, uc, 0x001caa50u, 0x001caa88u) &&
           add_code_hook(s, uc, 0x001c86a4u, 0x001c86a8u) &&
           add_code_hook(s, uc, 0x001d0900u, 0x001d0904u) &&
           add_code_hook(s, uc, 0x001d1348u, 0x001d134cu) &&
           add_code_hook(s, uc, 0x001bd000u, 0x001be000u) &&
           add_code_hook(s, uc, 0x001c4100u, 0x001c4600u) &&
           add_code_hook(s, uc, 0x10000ee4u, 0x10000ee8u) &&
           add_code_hook(s, uc, 0x10001078u, 0x1000107cu) &&
           add_code_hook(s, uc, 0x10001760u, 0x10001764u) &&
           add_code_hook(s, uc, 0x10001afcu, 0x10001b00u);
}

bool n1g_cpu_init(n1g_state_t *s) {
    for (int i = 0; i < N1G_CORE_COUNT; i++) {
        uc_err err = uc_open(UC_ARCH_ARM, UC_MODE_ARM | UC_MODE_LITTLE_ENDIAN, &s->cpu[i].uc);
        if (err != UC_ERR_OK) {
            n1g_log(s, "uc_open failed: %s", uc_strerror(err));
            return false;
        }
        if (s->opts.virtual_memmap) {
            err = uc_ctl_tlb_mode(s->cpu[i].uc, UC_TLB_VIRTUAL);
            if (err != UC_ERR_OK) {
                n1g_log(s, "uc_ctl_tlb_mode virtual failed: %s", uc_strerror(err));
                return false;
            }
            if (!add_hook_checked(s, s->cpu[i].uc, UC_HOOK_TLB_FILL, (void *)hook_tlb_fill, 1, 0)) {
                return false;
            }
        }
        s->cpu[i].running = true;
        s->cpu[i].halted = false;
        if (s->opts.trace_pc &&
            !add_hook_checked(s, s->cpu[i].uc, UC_HOOK_BLOCK, (void *)hook_block, 1, 0)) {
            return false;
        }
        if (!add_hook_checked(s, s->cpu[i].uc, UC_HOOK_INTR, (void *)hook_intr, 1, 0)) {
            return false;
        }
        if ((s->opts.boot_mode == N1G_BOOT_FLASH || s->opts.map_flash_zero || s->opts.virtual_memmap) &&
            !add_hook_checked(s,
                              s->cpu[i].uc,
                              UC_HOOK_MEM_WRITE,
                              (void *)hook_flash_write,
                              N1G_FLASH_BASE,
                              N1G_FLASH_BASE + N1G_FLASH_SIZE - 1u)) {
            return false;
        }
        if (!add_hook_checked(s,
                              s->cpu[i].uc,
                              UC_HOOK_MEM_WRITE,
                              (void *)hook_flash_write,
                              N1G_FLASH_ALIAS_BASE,
                              N1G_FLASH_ALIAS_BASE + N1G_FLASH_SIZE - 1u)) {
            return false;
        }
        if (s->opts.probe_pc_count > 0 && !add_probe_pc_hooks(s, s->cpu[i].uc)) {
            return false;
        }
        if (s->opts.profile == N1G_PROFILE_APPLE &&
            ((s->opts.verbose
                  ? !add_apple_verbose_hooks(s, s->cpu[i].uc)
                  : !add_apple_progress_hooks(s, s->cpu[i].uc)) ||
             (s->opts.verbose &&
              !add_hook_checked(s, s->cpu[i].uc, UC_HOOK_MEM_WRITE, (void *)hook_mem_write,
                                0x10700000u, 0x10800000u)) ||
             (s->opts.verbose &&
              !add_hook_checked(s, s->cpu[i].uc, UC_HOOK_MEM_WRITE, (void *)hook_mem_write,
                                N1G_FLASH_BASE, N1G_FLASH_BASE + N1G_FLASH_SIZE - 1u)) ||
             (s->opts.verbose &&
              !add_hook_checked(s, s->cpu[i].uc, UC_HOOK_MEM_READ, (void *)hook_mem_read,
                                N1G_FLASH_BASE, N1G_FLASH_BASE + N1G_FLASH_SIZE - 1u)) ||
             (s->opts.verbose &&
              !add_hook_checked(s, s->cpu[i].uc, UC_HOOK_MEM_WRITE, (void *)hook_mem_write,
                                0x11fa0000u, 0x12000000u)))) {
            return false;
        }
        if (s->opts.profile == N1G_PROFILE_APPLE &&
            !add_hook_checked(s,
                              s->cpu[i].uc,
                              UC_HOOK_MEM_WRITE,
                              (void *)hook_apple_key_gate_write,
                              0x10705468u,
                              0x1070546fu)) {
            return false;
        }
        if (!add_hook_checked(s, s->cpu[i].uc, UC_HOOK_MEM_INVALID, (void *)hook_mem_invalid, 1, 0)) {
            return false;
        }
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
        if (!map_ram_one(s, uc, N1G_SDRAM_BASE, N1G_SDRAM_SIZE, s->ram.sdram, "sdram0")) return false;
        if (!map_ram_one(s, uc, N1G_SDRAM_BASE + N1G_SDRAM_SIZE, N1G_SDRAM_SIZE, s->ram.sdram, "sdram1")) return false;
        if (!map_ram_one(s, uc, N1G_SDRAM_BASE + 2u * N1G_SDRAM_SIZE, N1G_SDRAM_SIZE, s->ram.sdram, "sdram2")) return false;
        if (!map_ram_one(s, uc, N1G_SDRAM_BASE + 3u * N1G_SDRAM_SIZE, N1G_SDRAM_SIZE, s->ram.sdram, "sdram3")) return false;
        if (s->opts.boot_mode == N1G_BOOT_FLASH || s->opts.map_flash_zero || s->opts.virtual_memmap) {
            if (!map_flash_one(s, uc)) return false;
            s->low0_map = N1G_LOW0_FLASH;
        } else if (!map_ram_one(s, uc, 0x00000000u, N1G_SDRAM_SIZE, s->ram.sdram, "low0")) {
            return false;
        } else {
            s->low0_map = N1G_LOW0_RAM;
        }
        if (!map_ram_one(s, uc, 0x02000000u, N1G_SDRAM_SIZE, s->ram.sdram, "low1")) return false;
        if (!map_ram_one(s, uc, 0x04000000u, N1G_SDRAM_SIZE, s->ram.sdram, "low2")) return false;
        if (!map_ram_one(s, uc, 0x06000000u, N1G_SDRAM_SIZE, s->ram.sdram, "low3")) return false;
        if (!map_ram_one(s, uc, N1G_SDRAM_ALIAS_BASE, N1G_SDRAM_SIZE, s->ram.sdram, "sdram_alias0")) return false;
        if (!map_ram_one(s, uc, N1G_SDRAM_ALIAS_BASE + N1G_SDRAM_SIZE, N1G_SDRAM_SIZE, s->ram.sdram, "sdram_alias1")) return false;
        if (!map_ram_one(s, uc, N1G_SDRAM_ALIAS_BASE + 2u * N1G_SDRAM_SIZE, N1G_SDRAM_SIZE, s->ram.sdram, "sdram_alias2")) return false;
        if (!map_ram_one(s, uc, N1G_SDRAM_ALIAS_BASE + 3u * N1G_SDRAM_SIZE, N1G_SDRAM_SIZE, s->ram.sdram, "sdram_alias3")) return false;
        if (!map_ram_one(s, uc, N1G_FASTRAM_BASE, N1G_FASTRAM_SIZE, s->ram.fastram, "fastram")) return false;

        if (!map_mmio(s, uc, (n1g_core_t)c, N1G_PP_BASE, 0x00100000u)) return false;
        if (!map_mmio(s, uc, (n1g_core_t)c, 0x64000000u, 0x00010000u)) return false;
        if (!map_mmio(s, uc, (n1g_core_t)c, N1G_PPCON_BASE, 0x00010000u)) return false;
        if (!map_mmio(s, uc, (n1g_core_t)c, N1G_EIDE_BASE, 0x00001000u)) return false;
        if (!map_mmio(s, uc, (n1g_core_t)c, N1G_USB_BASE, 0x00001000u)) return false;
        if (!map_mmio(s, uc, (n1g_core_t)c, N1G_MEMCON_BASE, 0x00010000u)) return false;
    }
    return true;
}

static bool remap_low0(n1g_state_t *s, n1g_low0_map_t kind) {
    if (s->low0_map == kind) {
        return true;
    }

    size_t old_size = s->low0_map == N1G_LOW0_FLASH ? N1G_FLASH_SIZE : N1G_SDRAM_SIZE;
    for (int c = 0; c < N1G_CORE_COUNT; c++) {
        uc_engine *uc = s->cpu[c].uc;
        uc_err err = uc_mem_unmap(uc, N1G_FLASH_BASE, old_size);
        if (err != UC_ERR_OK) {
            n1g_log(s, "low0 unmap failed size=0x%zx: %s", old_size, uc_strerror(err));
            return false;
        }
        if (kind == N1G_LOW0_FLASH) {
            if (!map_flash_one(s, uc)) return false;
        } else {
            if (!map_ram_one(s, uc, N1G_FLASH_BASE, N1G_SDRAM_SIZE, s->ram.sdram, "low0-remap")) return false;
        }
    }
    s->low0_map = kind;
    n1g_cpu_flush_tb(s);
    return true;
}

static bool map_flash_alias(n1g_state_t *s) {
    if (s->flash_alias_mapped) {
        return true;
    }

    for (int c = 0; c < N1G_CORE_COUNT; c++) {
        if (!map_flash_one_at(s, s->cpu[c].uc, N1G_FLASH_ALIAS_BASE, "alias200")) {
            return false;
        }
    }
    s->flash_alias_mapped = true;
    n1g_cpu_flush_tb(s);
    return true;
}

bool n1g_cpu_apply_memmap(n1g_state_t *s) {
    bool want_low_sdram = false;
    bool want_flash_alias = false;
    n1g_mmap_entry_t entries[8];

    decode_current_mmaps(s, entries);

    if (s->opts.virtual_memmap) {
        n1g_cpu_flush_tb(s);
        return true;
    }

    uint32_t translated = 0;
    if (n1g_mmap_translate(entries, 8u, 0x00000000u, N1G_MMAP_ACCESS_READ_DATA, &translated) &&
        translated == N1G_SDRAM_BASE &&
        n1g_mmap_translate(entries, 8u, 0x00000000u, N1G_MMAP_ACCESS_WRITE_DATA, &translated) &&
        translated == N1G_SDRAM_BASE &&
        n1g_mmap_translate(entries, 8u, 0x00000000u, N1G_MMAP_ACCESS_FETCH_CODE, &translated) &&
        translated == N1G_SDRAM_BASE) {
        want_low_sdram = true;
    }

    if (n1g_mmap_translate(entries, 8u, N1G_FLASH_ALIAS_BASE, N1G_MMAP_ACCESS_READ_DATA, &translated) &&
        translated == N1G_FLASH_BASE &&
        n1g_mmap_translate(entries, 8u, N1G_FLASH_ALIAS_BASE, N1G_MMAP_ACCESS_FETCH_CODE, &translated) &&
        translated == N1G_FLASH_BASE) {
        want_flash_alias = true;
    }

    if (want_low_sdram && !remap_low0(s, N1G_LOW0_RAM)) {
        return false;
    }
    if (want_flash_alias && !map_flash_alias(s)) {
        return false;
    }
    return true;
}

bool n1g_cpu_step_slice(n1g_state_t *s, n1g_core_t core, uint32_t max_insns) {
    if (s->tb_flush_pending) {
        s->tb_flush_pending = false;
        n1g_cpu_flush_tb(s);
    }
    if (s->cpu[core].halted || !s->cpu[core].running) {
        return true;
    }
    uint32_t pc = n1g_cpu_pc(s, core);
    uc_err err = uc_emu_start(s->cpu[core].uc, pc, UINT64_MAX, 0, max_insns);
    if (err != UC_ERR_OK) {
        n1g_log(s, "uc_emu_start core=%d pc=0x%08x failed: %s", core, pc, uc_strerror(err));
        return false;
    }
    s->cpu[core].guest_insns += max_insns;
    s->counters.guest_insns += max_insns;
    return true;
}

void n1g_cpu_raise_fiq(n1g_state_t *s, n1g_core_t core) {
    uint32_t cpsr = n1g_cpu_get_reg(s, core, UC_ARM_REG_CPSR);
    if (cpsr & 0x40u) {
        return;
    }

    uint32_t pc = n1g_cpu_pc(s, core);
    uint32_t lr_fiq = pc + 4u;
    /* FIQ entry: FIQ mode with both IRQ and FIQ masked. */
    uint32_t fiq_cpsr = (cpsr & ~0x3fu) | 0xd1u;

    uc_reg_write(s->cpu[core].uc, UC_ARM_REG_CPSR, &fiq_cpsr);
    uc_reg_write(s->cpu[core].uc, UC_ARM_REG_SPSR, &cpsr);
    uc_reg_write(s->cpu[core].uc, UC_ARM_REG_LR, &lr_fiq);
    uint32_t vector = (s->cachecon.regs[0] & 0x10u) ? s->evp.regs[7] : 0x1cu;
    uc_reg_write(s->cpu[core].uc, UC_ARM_REG_PC, &vector);

    s->cpu[core].halted = false;
    s->cpucon.ctl[core] &= ~0xe0000000u;
    s->counters.irq_count++;
    uc_emu_stop(s->cpu[core].uc);
}

void n1g_cpu_raise_irq(n1g_state_t *s, n1g_core_t core) {
    uint32_t cpsr = n1g_cpu_get_reg(s, core, UC_ARM_REG_CPSR);
    if (cpsr & 0x80u) {
        return;
    }

    uint32_t pc = n1g_cpu_pc(s, core);
    uint32_t lr_irq = pc + 4u;
    uint32_t irq_cpsr = (cpsr & ~0x3fu) | 0x92u;

    uc_reg_write(s->cpu[core].uc, UC_ARM_REG_CPSR, &irq_cpsr);
    uc_reg_write(s->cpu[core].uc, UC_ARM_REG_SPSR, &cpsr);
    uc_reg_write(s->cpu[core].uc, UC_ARM_REG_LR, &lr_irq);
    uint32_t vector = (s->cachecon.regs[0] & 0x10u) ? s->evp.regs[6] : 0x18u;
    uc_reg_write(s->cpu[core].uc, UC_ARM_REG_PC, &vector);

    s->cpu[core].halted = false;
    s->cpucon.ctl[core] &= ~0xe0000000u;
    s->counters.irq_count++;
    uc_emu_stop(s->cpu[core].uc);
}

void n1g_cpu_flush_tb(n1g_state_t *s) {
#if defined(uc_ctl_flush_tb)
    for (int i = 0; i < N1G_CORE_COUNT; i++) {
        if (s->cpu[i].uc) {
            (void)uc_ctl_flush_tb(s->cpu[i].uc);
#if defined(uc_ctl_flush_tlb)
            (void)uc_ctl_flush_tlb(s->cpu[i].uc);
#endif
        }
    }
#else
    (void)s;
#endif
}

uint32_t n1g_cpu_pc(n1g_state_t *s, n1g_core_t core) {
    uint32_t v = 0;
    uc_reg_read(s->cpu[core].uc, UC_ARM_REG_PC, &v);
    return v;
}

void n1g_cpu_set_reg(n1g_state_t *s, n1g_core_t core, int reg, uint32_t value) {
    uint32_t v = value;
    uc_reg_write(s->cpu[core].uc, reg, &v);
}

uint32_t n1g_cpu_get_reg(n1g_state_t *s, n1g_core_t core, int reg) {
    uint32_t v = 0;
    uc_reg_read(s->cpu[core].uc, reg, &v);
    return v;
}

void n1g_cpu_set_gpr(n1g_state_t *s, n1g_core_t core, unsigned reg, uint32_t value) {
    if (reg >= 16) {
        return;
    }
    n1g_cpu_set_reg(s, core, arm_regs[reg], value);
}

uint32_t n1g_cpu_get_gpr(n1g_state_t *s, n1g_core_t core, unsigned reg) {
    if (reg >= 16) {
        return 0;
    }
    return n1g_cpu_get_reg(s, core, arm_regs[reg]);
}
