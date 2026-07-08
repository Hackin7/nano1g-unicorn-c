#include "nano1g/cpu_unicorn.h"
#include "nano1g/bus.h"
#include "nano1g/devices.h"
#include "nano1g/disk_ata.h"
#include "nano1g/boot_reset.h"
#include "nano1g/firmware.h"
#include "nano1g/ram.h"
#include "nano1g/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    puts("nano1g --profile apple|rockbox [--firmware PATH] [--flash-rom PATH] [--disk PATH] [--ppm PATH]");
    puts("       [--max-insns N] [--slice-insns N] [--timer-divider N] [--load-addr ADDR] [--entry ADDR]");
    puts("       [--dump32 ADDR] [--dump-count N]");
    puts("       [--boot-mode direct|flash] [--firmware-from-disk] [--map-flash-zero] [--input SCRIPT]");
    puts("       [--trace-pc] [--trace-mmio] [--verbose]");
}

static bool parse_profile(const char *v, n1g_profile_t *out) {
    if (strcmp(v, "apple") == 0) {
        *out = N1G_PROFILE_APPLE;
        return true;
    }
    if (strcmp(v, "rockbox") == 0) {
        *out = N1G_PROFILE_ROCKBOX;
        return true;
    }
    return false;
}

static bool parse_boot_mode(const char *v, n1g_boot_mode_t *out) {
    if (strcmp(v, "direct") == 0) {
        *out = N1G_BOOT_DIRECT;
        return true;
    }
    if (strcmp(v, "flash") == 0) {
        *out = N1G_BOOT_FLASH;
        return true;
    }
    return false;
}

static n1g_opts_t parse_args(int argc, char **argv) {
    n1g_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.profile = N1G_PROFILE_ROCKBOX;
    opts.boot_mode = N1G_BOOT_DIRECT;
    opts.max_insns = 20000000u;
    opts.slice_insns = 1;
    opts.timer_divider = 20;
    opts.load_addr = N1G_SDRAM_BASE;
    opts.dump_count = 1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage();
            exit(0);
        } else if (strcmp(a, "--profile") == 0 && i + 1 < argc) {
            if (!parse_profile(argv[++i], &opts.profile)) {
                n1g_die("invalid profile: %s", argv[i]);
            }
        } else if (strcmp(a, "--firmware") == 0 && i + 1 < argc) {
            opts.firmware_path = argv[++i];
        } else if (strcmp(a, "--flash-rom") == 0 && i + 1 < argc) {
            opts.flash_path = argv[++i];
        } else if (strcmp(a, "--disk") == 0 && i + 1 < argc) {
            opts.disk_path = argv[++i];
        } else if (strcmp(a, "--ppm") == 0 && i + 1 < argc) {
            opts.ppm_path = argv[++i];
        } else if (strcmp(a, "--max-insns") == 0 && i + 1 < argc) {
            opts.max_insns = n1g_parse_u64(argv[++i], "max-insns");
        } else if (strcmp(a, "--slice-insns") == 0 && i + 1 < argc) {
            opts.slice_insns = n1g_parse_u32(argv[++i], "slice-insns");
            if (opts.slice_insns == 0) {
                n1g_die("--slice-insns must be nonzero");
            }
        } else if (strcmp(a, "--timer-divider") == 0 && i + 1 < argc) {
            opts.timer_divider = n1g_parse_u32(argv[++i], "timer-divider");
            if (opts.timer_divider == 0) {
                n1g_die("--timer-divider must be nonzero");
            }
        } else if (strcmp(a, "--load-addr") == 0 && i + 1 < argc) {
            opts.load_addr = n1g_parse_u32(argv[++i], "load-addr");
        } else if (strcmp(a, "--entry") == 0 && i + 1 < argc) {
            opts.entry = n1g_parse_u32(argv[++i], "entry");
            opts.entry_set = true;
        } else if (strcmp(a, "--dump32") == 0 && i + 1 < argc) {
            opts.dump_addr = n1g_parse_u32(argv[++i], "dump32");
            opts.dump_set = true;
        } else if (strcmp(a, "--dump-count") == 0 && i + 1 < argc) {
            opts.dump_count = n1g_parse_u32(argv[++i], "dump-count");
            if (opts.dump_count == 0 || opts.dump_count > 32) {
                n1g_die("--dump-count must be between 1 and 32");
            }
        } else if (strcmp(a, "--boot-mode") == 0 && i + 1 < argc) {
            if (!parse_boot_mode(argv[++i], &opts.boot_mode)) {
                n1g_die("invalid boot mode: %s", argv[i]);
            }
        } else if (strcmp(a, "--firmware-from-disk") == 0) {
            opts.firmware_from_disk = true;
        } else if (strcmp(a, "--map-flash-zero") == 0) {
            opts.map_flash_zero = true;
        } else if (strcmp(a, "--input") == 0 && i + 1 < argc) {
            opts.input_script = argv[++i];
        } else if (strcmp(a, "--trace-pc") == 0) {
            opts.trace_pc = true;
        } else if (strcmp(a, "--trace-mmio") == 0) {
            opts.trace_mmio = true;
        } else if (strcmp(a, "--verbose") == 0) {
            opts.verbose = true;
        } else {
            n1g_die("unknown or incomplete option: %s", a);
        }
    }
    if (opts.boot_mode == N1G_BOOT_DIRECT && !opts.firmware_path && !opts.firmware_from_disk) {
        n1g_die("--firmware or --firmware-from-disk is required in direct boot mode");
    }
    if (opts.firmware_from_disk && !opts.disk_path) {
        n1g_die("--firmware-from-disk requires --disk");
    }
    if (opts.boot_mode == N1G_BOOT_FLASH && !opts.flash_path) {
        n1g_die("--flash-rom is required in flash boot mode");
    }
    if (opts.boot_mode == N1G_BOOT_FLASH && opts.map_flash_zero) {
        n1g_die("--map-flash-zero is only meaningful in direct boot mode");
    }
    return opts;
}

static void destroy_state(n1g_state_t *s) {
    n1g_cpu_destroy(s);
    n1g_disk_destroy(s);
    n1g_ram_destroy(s);
}

static uint32_t read32_or_zero(n1g_state_t *s, uint32_t addr) {
    uint32_t value = 0;
    (void)n1g_ram_read(s, addr, 4, &value);
    return value;
}

static bool apple_sample_lcd_sized_buffer(n1g_state_t *s,
                                          uint32_t src,
                                          uint32_t *nonzero_words,
                                          uint64_t *hash) {
    const uint32_t bytes = N1G_LCD_W * N1G_LCD_H * 2u;
    *nonzero_words = 0;
    *hash = 1469598103934665603ull;
    for (uint32_t offset = 0; offset < bytes; offset += 4u) {
        uint32_t word = 0;
        if (!n1g_ram_read(s, src + offset, 4, &word)) {
            return false;
        }
        *hash ^= word;
        *hash *= 1099511628211ull;
        if (word != 0) {
            (*nonzero_words)++;
        }
    }
    return true;
}

static void apple_log_lcd_buffer_probes(n1g_state_t *s) {
    if (s->opts.profile != N1G_PROFILE_APPLE || !s->opts.verbose) {
        return;
    }

    const uint32_t active = read32_or_zero(s, 0x10705b48u);
    const uint32_t candidates[] = {
        read32_or_zero(s, active),
        read32_or_zero(s, active + 4u),
        read32_or_zero(s, 0x10705b38u),
        read32_or_zero(s, 0x10705b40u),
        read32_or_zero(s, 0x10705b44u),
        read32_or_zero(s, 0x10705b48u),
    };
    n1g_log(s,
            "apple_lcd_globals g5b38=0x%08x,0x%08x,0x%08x,0x%08x g5b48=0x%08x factory=0x%08x display=0x%08x",
            read32_or_zero(s, 0x10705b38u),
            read32_or_zero(s, 0x10705b3cu),
            read32_or_zero(s, 0x10705b40u),
            read32_or_zero(s, 0x10705b44u),
            active,
            read32_or_zero(s, 0x10705ba0u),
            read32_or_zero(s, 0x10707698u));

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        uint32_t nz = 0;
        uint64_t hash = 0;
        if (candidates[i] == 0) {
            continue;
        }
        n1g_log(s,
                "apple_lcd_descriptor_candidate[%u] desc=0x%08x words=0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x",
                (unsigned)i,
                candidates[i],
                read32_or_zero(s, candidates[i]),
                read32_or_zero(s, candidates[i] + 4u),
                read32_or_zero(s, candidates[i] + 8u),
                read32_or_zero(s, candidates[i] + 12u),
                read32_or_zero(s, candidates[i] + 16u),
                read32_or_zero(s, candidates[i] + 20u));
        if (apple_sample_lcd_sized_buffer(s, candidates[i], &nz, &hash)) {
            n1g_log(s, "apple_lcd_candidate[%u] addr=0x%08x nonzero_words=%u hash=0x%016llx",
                    (unsigned)i, candidates[i], nz, (unsigned long long)hash);
        }
    }

    uint32_t best_addr[4] = {0};
    uint32_t best_nz[4] = {0};
    uint64_t best_hash[4] = {0};
    const uint32_t frame_bytes = N1G_LCD_W * N1G_LCD_H * 2u;
    for (uint32_t addr = 0x11f00000u; addr + frame_bytes <= 0x12000000u; addr += 0x1000u) {
        uint32_t nz = 0;
        uint64_t hash = 0;
        if (!apple_sample_lcd_sized_buffer(s, addr, &nz, &hash)) {
            continue;
        }
        for (size_t slot = 0; slot < 4; slot++) {
            if (nz <= best_nz[slot]) {
                continue;
            }
            for (size_t move = 3; move > slot; move--) {
                best_addr[move] = best_addr[move - 1u];
                best_nz[move] = best_nz[move - 1u];
                best_hash[move] = best_hash[move - 1u];
            }
            best_addr[slot] = addr;
            best_nz[slot] = nz;
            best_hash[slot] = hash;
            break;
        }
    }
    n1g_log(s,
            "apple_lcd_scan_high best=0x%08x/%u/0x%016llx 0x%08x/%u/0x%016llx 0x%08x/%u/0x%016llx 0x%08x/%u/0x%016llx",
            best_addr[0],
            best_nz[0],
            (unsigned long long)best_hash[0],
            best_addr[1],
            best_nz[1],
            (unsigned long long)best_hash[1],
            best_addr[2],
            best_nz[2],
            (unsigned long long)best_hash[2],
            best_addr[3],
            best_nz[3],
            (unsigned long long)best_hash[3]);
}

int main(int argc, char **argv) {
    n1g_state_t s;
    memset(&s, 0, sizeof(s));
    s.opts = parse_args(argc, argv);
    memset(s.flash.bytes, 0xff, sizeof(s.flash.bytes));

    if (s.opts.input_script) {
        n1g_info(&s, "input script accepted for future injection: %s", s.opts.input_script);
    }
    if (!n1g_ram_init(&s)) {
        n1g_die("failed to allocate RAM");
    }
    n1g_dev_evp_init(&s);
    if (!n1g_disk_load(&s, s.opts.disk_path)) {
        destroy_state(&s);
        return 1;
    }
    if (!n1g_load_flash_rom(&s)) {
        destroy_state(&s);
        return 1;
    }
    if (!n1g_cpu_init(&s)) {
        destroy_state(&s);
        return 1;
    }
    if (!n1g_boot_reset(&s)) {
        destroy_state(&s);
        return 1;
    }
    if (!n1g_cpu_map_memory(&s)) {
        destroy_state(&s);
        return 1;
    }

    n1g_info(&s, "start max_insns=%llu slice_insns=%u timer_divider=%u",
             (unsigned long long)s.opts.max_insns, s.opts.slice_insns, s.opts.timer_divider);
    uint64_t remaining = s.opts.max_insns;
    while (remaining > 0) {
        uint32_t slice = s.opts.slice_insns;
        if (remaining < slice) {
            slice = (uint32_t)remaining;
        }
        if (!n1g_cpu_step_slice(&s, N1G_CORE_CPU, slice)) {
            break;
        }
        if (!s.cpu[N1G_CORE_COP].halted) {
            if (!n1g_cpu_step_slice(&s, N1G_CORE_COP, slice)) {
                break;
            }
        }
        n1g_bus_tick(&s);
        remaining -= slice;
    }

    apple_log_lcd_buffer_probes(&s);
    n1g_dev_lcd2_write_ppm(&s, s.opts.ppm_path);
    if (s.opts.dump_set) {
        char line[512];
        int n = snprintf(line, sizeof(line), "dump32 addr=0x%08x", s.opts.dump_addr);
        for (uint32_t i = 0; i < s.opts.dump_count && n > 0 && (size_t)n < sizeof(line); i++) {
            uint32_t addr = s.opts.dump_addr + i * 4u;
            uint32_t value = n1g_bus_read(&s, N1G_CORE_CPU, addr, 4);
            n += snprintf(line + n, sizeof(line) - (size_t)n, " 0x%08x", value);
        }
        n1g_info(&s, "%s", line);
    }
    n1g_info(&s,
             "summary guest_insns=%llu ticks=%llu mmio_r=%llu mmio_w=%llu lcd_words=%llu disk_reads=%llu irq=%llu pc=0x%08x",
             (unsigned long long)s.counters.guest_insns,
             (unsigned long long)s.counters.device_ticks,
             (unsigned long long)s.counters.mmio_reads,
             (unsigned long long)s.counters.mmio_writes,
             (unsigned long long)s.counters.lcd_words,
             (unsigned long long)s.counters.disk_reads,
             (unsigned long long)s.counters.irq_count,
             n1g_cpu_pc(&s, N1G_CORE_CPU));
    n1g_info(&s,
             "cores cpu_pc=0x%08x cop_pc=0x%08x cpu_halted=%u cop_halted=%u cpu_ctl=0x%08x cop_ctl=0x%08x cpu_insns=%llu cop_insns=%llu",
             n1g_cpu_pc(&s, N1G_CORE_CPU),
             n1g_cpu_pc(&s, N1G_CORE_COP),
             s.cpu[N1G_CORE_CPU].halted ? 1u : 0u,
             s.cpu[N1G_CORE_COP].halted ? 1u : 0u,
             s.cpucon.ctl[N1G_CORE_CPU],
             s.cpucon.ctl[N1G_CORE_COP],
             (unsigned long long)s.cpu[N1G_CORE_CPU].guest_insns,
             (unsigned long long)s.cpu[N1G_CORE_COP].guest_insns);
    if (s.opts.profile == N1G_PROFILE_APPLE) {
        uint32_t active = read32_or_zero(&s, 0x10705b48u);
        uint32_t queue_slot = read32_or_zero(&s, 0x10743194u);
        n1g_info(&s,
                "apple_pc_hist plateau_24dxx=%llu event_2a=%llu ui_483xx=%llu audio_99dxx=%llu mp3_c8cxx=%llu event_cb_1526xx=%llu task_1bdxx=%llu fs_1c42xx=%llu other_fw=%llu other=%llu",
                (unsigned long long)s.counters.apple_pc_hits[0],
                (unsigned long long)s.counters.apple_pc_hits[1],
                (unsigned long long)s.counters.apple_pc_hits[2],
                (unsigned long long)s.counters.apple_pc_hits[3],
                (unsigned long long)s.counters.apple_pc_hits[4],
                (unsigned long long)s.counters.apple_pc_hits[5],
                (unsigned long long)s.counters.apple_pc_hits[6],
                (unsigned long long)s.counters.apple_pc_hits[7],
                (unsigned long long)s.counters.apple_pc_hits[8],
                (unsigned long long)s.counters.apple_pc_hits[9]);
        n1g_info(&s,
                "apple_ui_hits create_24c48=%llu create_25024=%llu queue_send_32840=%llu msg_post_48060=%llu msg_done_480ac=%llu lang_loop_4ee20=%llu lang_post_4eeb4=%llu view_deliver_5410c=%llu lcd_dirty_53b18=%llu ui_qread_d0bb4=%llu ui_gate_d0c54=%llu subscriber_reg_17d260=%llu post_wait_48098=%llu sched14_1c5188=%llu sched_run_1c5808=%llu dispatch_ret_1c6078=%llu",
                (unsigned long long)s.counters.apple_ui_hits[0],
                (unsigned long long)s.counters.apple_ui_hits[1],
                (unsigned long long)s.counters.apple_ui_hits[2],
                (unsigned long long)s.counters.apple_ui_hits[3],
                (unsigned long long)s.counters.apple_ui_hits[4],
                (unsigned long long)s.counters.apple_ui_hits[5],
                (unsigned long long)s.counters.apple_ui_hits[6],
                (unsigned long long)s.counters.apple_ui_hits[7],
                (unsigned long long)s.counters.apple_ui_hits[8],
                (unsigned long long)s.counters.apple_ui_hits[9],
                (unsigned long long)s.counters.apple_ui_hits[10],
                (unsigned long long)s.counters.apple_ui_hits[11],
                (unsigned long long)s.counters.apple_ui_hits[12],
                (unsigned long long)s.counters.apple_ui_hits[13],
                (unsigned long long)s.counters.apple_ui_hits[14],
                (unsigned long long)s.counters.apple_ui_hits[15]);
        n1g_info(&s,
                "apple_lcd_task_hits entry_53580=%llu e53584=%llu e53588=%llu e53590=%llu e53594=%llu e535a0=%llu e535a4=%llu q_53b04=%llu q_53b08=%llu q_53b0c=%llu q_53b10=%llu q_53b14=%llu dirty_53b18=%llu post_53b20=%llu submit_53b38=%llu wait_53db8=%llu flush_53f28=%llu flush_53f30=%llu",
                (unsigned long long)s.counters.apple_lcd_task_hits[0],
                (unsigned long long)s.counters.apple_lcd_task_hits[1],
                (unsigned long long)s.counters.apple_lcd_task_hits[2],
                (unsigned long long)s.counters.apple_lcd_task_hits[3],
                (unsigned long long)s.counters.apple_lcd_task_hits[4],
                (unsigned long long)s.counters.apple_lcd_task_hits[5],
                (unsigned long long)s.counters.apple_lcd_task_hits[6],
                (unsigned long long)s.counters.apple_lcd_task_hits[7],
                (unsigned long long)s.counters.apple_lcd_task_hits[8],
                (unsigned long long)s.counters.apple_lcd_task_hits[9],
                (unsigned long long)s.counters.apple_lcd_task_hits[10],
                (unsigned long long)s.counters.apple_lcd_task_hits[11],
                (unsigned long long)s.counters.apple_lcd_task_hits[12],
                (unsigned long long)s.counters.apple_lcd_task_hits[13],
                (unsigned long long)s.counters.apple_lcd_task_hits[14],
                (unsigned long long)s.counters.apple_lcd_task_hits[15],
                (unsigned long long)s.counters.apple_lcd_task_hits[16],
                (unsigned long long)s.counters.apple_lcd_task_hits[17]);
        n1g_info(&s,
                "apple_exit active=0x%08x active_words=0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x queue_slot=0x%08x queue_head=0x%08x timer=0x%08x,0x%08x,0x%08x,0x%08x intc_cpu=0x%08x/0x%08x/0x%08x/0x%08x intc_cop=0x%08x/0x%08x/0x%08x/0x%08x",
                active,
                read32_or_zero(&s, active),
                read32_or_zero(&s, active + 4u),
                read32_or_zero(&s, active + 8u),
                read32_or_zero(&s, active + 12u),
                read32_or_zero(&s, active + 16u),
                read32_or_zero(&s, active + 20u),
                queue_slot,
                read32_or_zero(&s, queue_slot),
                s.timer.cfg[0],
                s.timer.val[0],
                s.timer.cfg[1],
                s.timer.val[1],
                s.intc.cpu_status,
                s.intc.cpu_enable,
                s.intc.hi_cpu_status,
                s.intc.hi_cpu_enable,
                s.intc.cop_status,
                s.intc.cop_enable,
                s.intc.hi_cop_status,
                s.intc.hi_cop_enable);
        n1g_info(&s,
                "apple_objects lang_state=0x%08x,0x%08x,0x%08x,0x%08x lang_view=0x%08x,0x%08x,0x%08x,0x%08x lang_list=0x%08x,0x%08x,0x%08x,0x%08x subscriber=0x%08x,0x%08x,0x%08x,0x%08x view=0x%08x,0x%08x,0x%08x,0x%08x",
                read32_or_zero(&s, 0x1070597cu),
                read32_or_zero(&s, 0x10705980u),
                read32_or_zero(&s, 0x10705984u),
                read32_or_zero(&s, 0x10705988u),
                read32_or_zero(&s, 0x10746ef8u),
                read32_or_zero(&s, 0x10746efcu),
                read32_or_zero(&s, 0x10746f00u),
                read32_or_zero(&s, 0x10746f04u),
                read32_or_zero(&s, 0x10743164u),
                read32_or_zero(&s, 0x10743168u),
                read32_or_zero(&s, 0x1074316cu),
                read32_or_zero(&s, 0x10743170u),
                read32_or_zero(&s, 0x10902fdcu),
                read32_or_zero(&s, 0x10902fe0u),
                read32_or_zero(&s, 0x10902fe4u),
                read32_or_zero(&s, 0x10902fe8u),
                read32_or_zero(&s, 0x11f8fe24u),
                read32_or_zero(&s, 0x11f8fe28u),
                read32_or_zero(&s, 0x11f8fe2cu),
                read32_or_zero(&s, 0x11f8fe30u));
        n1g_info(&s,
                "apple_language_context ctrl=0x%08x,0x%08x,0x%08x,0x%08x ctrl230=0x%08x state04=0x%08x state14=0x%08x subscriber_global=0x%08x,0x%08x,0x%08x,0x%08x",
                read32_or_zero(&s, 0x11f9d560u),
                read32_or_zero(&s, 0x11f9d564u),
                read32_or_zero(&s, 0x11f9d568u),
                read32_or_zero(&s, 0x11f9d56cu),
                read32_or_zero(&s, 0x11f9d790u),
                read32_or_zero(&s, 0x10705980u),
                read32_or_zero(&s, 0x10705990u),
                read32_or_zero(&s, 0x10705a20u),
                read32_or_zero(&s, 0x10705a24u),
                read32_or_zero(&s, 0x10705a28u),
                read32_or_zero(&s, 0x10705a2cu));
    }

    destroy_state(&s);
    return 0;
}
