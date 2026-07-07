#include "nano1g/bus.h"
#include "nano1g/cpu_unicorn.h"
#include "nano1g/devices.h"
#include "nano1g/disk_ata.h"
#include "nano1g/hle_boot.h"
#include "nano1g/ram.h"
#include "nano1g/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    puts("nano1g --profile apple|rockbox --firmware PATH [--disk PATH] [--ppm PATH]");
    puts("       [--max-insns N] [--slice-insns N] [--load-addr ADDR] [--entry ADDR]");
    puts("       [--input SCRIPT] [--trace-pc]");
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

static n1g_opts_t parse_args(int argc, char **argv) {
    n1g_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.profile = N1G_PROFILE_ROCKBOX;
    opts.max_insns = 20000000u;
    opts.slice_insns = 1;
    opts.load_addr = N1G_SDRAM_BASE;

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
        } else if (strcmp(a, "--load-addr") == 0 && i + 1 < argc) {
            opts.load_addr = n1g_parse_u32(argv[++i], "load-addr");
        } else if (strcmp(a, "--entry") == 0 && i + 1 < argc) {
            opts.entry = n1g_parse_u32(argv[++i], "entry");
            opts.entry_set = true;
        } else if (strcmp(a, "--input") == 0 && i + 1 < argc) {
            opts.input_script = argv[++i];
        } else if (strcmp(a, "--trace-pc") == 0) {
            opts.trace_pc = true;
        } else {
            n1g_die("unknown or incomplete option: %s", a);
        }
    }
    if (!opts.firmware_path) {
        n1g_die("--firmware is required");
    }
    return opts;
}

static void destroy_state(n1g_state_t *s) {
    n1g_cpu_destroy(s);
    n1g_disk_destroy(s);
    n1g_ram_destroy(s);
}

int main(int argc, char **argv) {
    n1g_state_t s;
    memset(&s, 0, sizeof(s));
    s.opts = parse_args(argc, argv);

    if (s.opts.input_script) {
        n1g_log(&s, "input script accepted for future injection: %s", s.opts.input_script);
    }
    if (!n1g_ram_init(&s)) {
        n1g_die("failed to allocate RAM");
    }
    if (!n1g_disk_load(&s, s.opts.disk_path)) {
        destroy_state(&s);
        return 1;
    }
    if (!n1g_cpu_init(&s)) {
        destroy_state(&s);
        return 1;
    }
    if (!n1g_hle_boot(&s)) {
        destroy_state(&s);
        return 1;
    }
    if (!n1g_cpu_map_memory(&s)) {
        destroy_state(&s);
        return 1;
    }

    n1g_log(&s, "start max_insns=%llu slice_insns=%u",
            (unsigned long long)s.opts.max_insns, s.opts.slice_insns);
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

    n1g_dev_lcd2_write_ppm(&s, s.opts.ppm_path);
    n1g_log(&s,
            "summary guest_insns=%llu ticks=%llu mmio_r=%llu mmio_w=%llu lcd_words=%llu disk_reads=%llu irq=%llu pc=0x%08x",
            (unsigned long long)s.counters.guest_insns,
            (unsigned long long)s.counters.device_ticks,
            (unsigned long long)s.counters.mmio_reads,
            (unsigned long long)s.counters.mmio_writes,
            (unsigned long long)s.counters.lcd_words,
            (unsigned long long)s.counters.disk_reads,
            (unsigned long long)s.counters.irq_count,
            n1g_cpu_pc(&s, N1G_CORE_CPU));

    destroy_state(&s);
    return 0;
}
