#include "nano1g/cpu_unicorn.h"
#include "nano1g/bus.h"
#include "nano1g/devices.h"
#include "nano1g/disk_ata.h"
#include "nano1g/boot_reset.h"
#include "nano1g/firmware.h"
#include "nano1g/input_script.h"
#include "nano1g/ram.h"
#include "nano1g/trace.h"
#include "nano1g/web_frontend.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

static volatile sig_atomic_t stop_requested;

typedef struct n1g_host_profile {
    uint64_t started_ns;
    uint64_t cpu_ns;
    uint64_t cop_ns;
    uint64_t bus_ns;
    uint64_t input_ns;
    uint64_t web_ns;
    uint64_t slices;
    uint64_t cpu_calls;
    uint64_t cop_calls;
    uint64_t web_polls;
} n1g_host_profile_t;

static uint64_t host_profile_now_ns(void) {
#if defined(_WIN32)
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (frequency.QuadPart == 0 && !QueryPerformanceFrequency(&frequency)) {
        return 0;
    }
    if (!QueryPerformanceCounter(&counter)) {
        return 0;
    }
    uint64_t whole = (uint64_t)(counter.QuadPart / frequency.QuadPart);
    uint64_t remainder = (uint64_t)(counter.QuadPart % frequency.QuadPart);
    return whole * 1000000000ull +
           remainder * 1000000000ull / (uint64_t)frequency.QuadPart;
#else
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

static void host_profile_add(uint64_t *total_ns, uint64_t started_ns) {
    uint64_t finished_ns = host_profile_now_ns();
    if (finished_ns >= started_ns) {
        *total_ns += finished_ns - started_ns;
    }
}

static void host_profile_report(n1g_state_t *s, const n1g_host_profile_t *profile) {
    uint64_t finished_ns = host_profile_now_ns();
    uint64_t total_ns = finished_ns >= profile->started_ns
                            ? finished_ns - profile->started_ns
                            : 0;
    uint64_t accounted_ns = profile->cpu_ns + profile->cop_ns + profile->bus_ns +
                            profile->input_ns + profile->web_ns;
    uint64_t loop_ns = total_ns >= accounted_ns ? total_ns - accounted_ns : 0;
    n1g_info(s,
             "host_profile total_ns=%llu cpu_ns=%llu cop_ns=%llu bus_ns=%llu input_ns=%llu web_ns=%llu loop_ns=%llu slices=%llu cpu_calls=%llu cop_calls=%llu web_polls=%llu",
             (unsigned long long)total_ns,
             (unsigned long long)profile->cpu_ns,
             (unsigned long long)profile->cop_ns,
             (unsigned long long)profile->bus_ns,
             (unsigned long long)profile->input_ns,
             (unsigned long long)profile->web_ns,
             (unsigned long long)loop_ns,
             (unsigned long long)profile->slices,
             (unsigned long long)profile->cpu_calls,
             (unsigned long long)profile->cop_calls,
             (unsigned long long)profile->web_polls);
    n1g_bus_host_profile_report(s);
}

static void handle_stop_signal(int sig) {
    (void)sig;
    stop_requested = 1;
}

static const char apple_stage0_label[] = "Apple stage0 canary";
static const char apple_direct_label[] = "Apple OS direct diagnostic";
static const char apple_flash_label[] = "Apple official boot";
static const char ipodlinux_label[] = "iPod Linux (experimental)";
static const char rockbox_label[] = "Rockbox";

static const char apple_stage0_fw[] = "tmp/stage0-sysinfo-osos-probe.bin";
static const char apple_direct_fw[] = "../artifacts/firmware/apple_nano_14.5.3.1_fw.bin";
static const char apple_default_flash_rom[] = "../artifacts/firmware/apple_nano_1g_bootrom.bin";
static const char apple_disk[] = "../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img";
static const char ipodlinux_fw[] = "../artifacts/ipodlinux/kernel.bin";
static const char ipodlinux_disk[] = "../artifacts/ipodlinux/userland.img";
static const char rockbox_fw[] = "../artifacts/firmware/rockbox.ipod";
static const char rockbox_disk[] = "tmp/ipodhd-rockbox-nano-content-gpt.img";

static const char *apple_flash_rom_path(void) {
    const char *env = getenv("NANO1G_APPLE_BOOTROM");
    return (env && env[0]) ? env : apple_default_flash_rom;
}

static void apply_run_preset(n1g_opts_t *opts, const char *preset);

static void usage(void) {
    puts("nano1g [--run rockbox|ipodlinux|apple-stage0|apple-direct|apple-official|apple-flash]");
    puts("       [--profile apple|rockbox] [--firmware PATH] [--flash-rom PATH] [--disk PATH] [--disk-out PATH] [--ppm PATH]");
    puts("       [--max-insns N] [--slice-insns N] [--timer-divider N] [--rtc-usec-per-tick N]");
    puts("       [--load-addr ADDR] [--entry ADDR]");
    puts("       [--dump32 ADDR] [--dump-count N]");
    puts("       [--boot-mode direct|flash] [--firmware-from-disk] [--map-flash-zero] [--virtual-memmap] [--ram-fill-zero] [--input SCRIPT]");
    puts("       [--battery-percent N] [--main-charger] [--usb-charger]");
    puts("       [--web PORT] [--web-no-hold] [--run-forever]");
    puts("       [--trace-pc] [--trace-mmio] [--apple-diagnostics] [--host-profile] [--verbose]");
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
    opts.rtc_usec_per_tick = 1;
    opts.load_addr = N1G_SDRAM_BASE;
    opts.dump_count = 1;
    opts.battery_percent = 100u;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage();
            exit(0);
        } else if (strcmp(a, "--run") == 0 && i + 1 < argc) {
            apply_run_preset(&opts, argv[++i]);
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
        } else if (strcmp(a, "--disk-out") == 0 && i + 1 < argc) {
            opts.disk_out_path = argv[++i];
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
        } else if (strcmp(a, "--rtc-usec-per-tick") == 0 && i + 1 < argc) {
            opts.rtc_usec_per_tick = n1g_parse_u32(argv[++i], "rtc-usec-per-tick");
            if (opts.rtc_usec_per_tick == 0) {
                n1g_die("--rtc-usec-per-tick must be nonzero");
            }
        } else if (strcmp(a, "--load-addr") == 0 && i + 1 < argc) {
            opts.load_addr = n1g_parse_u32(argv[++i], "load-addr");
        } else if (strcmp(a, "--entry") == 0 && i + 1 < argc) {
            opts.entry = n1g_parse_u32(argv[++i], "entry");
            opts.entry_set = true;
        } else if (strcmp(a, "--probe-pc") == 0 && i + 1 < argc) {
            if (opts.probe_pc_count >= 16u) {
                n1g_die("--probe-pc supports at most 16 addresses");
            }
            opts.probe_pc[opts.probe_pc_count++] = n1g_parse_u32(argv[++i], "probe-pc");
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
        } else if (strcmp(a, "--virtual-memmap") == 0) {
            opts.virtual_memmap = true;
        } else if (strcmp(a, "--ram-fill-zero") == 0) {
            opts.ram_fill_zero = true;
        } else if (strcmp(a, "--input") == 0 && i + 1 < argc) {
            opts.input_script = argv[++i];
        } else if (strcmp(a, "--battery-percent") == 0 && i + 1 < argc) {
            opts.battery_percent = n1g_parse_u32(argv[++i], "battery-percent");
            if (opts.battery_percent > 100u) {
                n1g_die("--battery-percent must be between 0 and 100");
            }
        } else if (strcmp(a, "--main-charger") == 0) {
            opts.main_charger_connected = true;
        } else if (strcmp(a, "--usb-charger") == 0) {
            opts.usb_charger_connected = true;
        } else if (strcmp(a, "--web") == 0 && i + 1 < argc) {
            uint32_t port = n1g_parse_u32(argv[++i], "web");
            if (port == 0 || port > 65535u) {
                n1g_die("--web port must be between 1 and 65535");
            }
            opts.web_enabled = true;
            opts.web_port = (uint16_t)port;
        } else if (strcmp(a, "--web-no-hold") == 0) {
            opts.web_no_hold = true;
        } else if (strcmp(a, "--run-forever") == 0) {
            opts.run_forever = true;
        } else if (strcmp(a, "--trace-pc") == 0) {
            opts.trace_pc = true;
        } else if (strcmp(a, "--trace-mmio") == 0) {
            opts.trace_mmio = true;
        } else if (strcmp(a, "--apple-diagnostics") == 0) {
            opts.apple_diagnostics = true;
        } else if (strcmp(a, "--host-profile") == 0) {
            opts.host_profile = true;
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
    if (opts.disk_out_path && !opts.disk_path) {
        n1g_die("--disk-out requires a source disk");
    }
    return opts;
}

static void destroy_state(n1g_state_t *s) {
    n1g_cpu_destroy(s);
    n1g_disk_destroy(s);
    n1g_ram_destroy(s);
}

static void infer_run_label(n1g_opts_t *opts) {
    if (opts->run_label) {
        return;
    }
    if (opts->profile == N1G_PROFILE_APPLE) {
        if (opts->boot_mode == N1G_BOOT_FLASH) {
            opts->run_label = apple_flash_label;
        } else if (opts->firmware_path && strstr(opts->firmware_path, "stage0")) {
            opts->run_label = apple_stage0_label;
        } else {
            opts->run_label = apple_direct_label;
        }
    } else {
        opts->run_label = rockbox_label;
    }
}

static bool disk_output_active(const n1g_opts_t *opts) {
    if (!opts->disk_out_path || !opts->disk_path || !opts->disk_seed_path) {
        return false;
    }
    return strcmp(opts->disk_path, opts->disk_seed_path) == 0 ||
           strcmp(opts->disk_path, opts->disk_out_path) == 0;
}

static bool save_disk_output(n1g_state_t *s) {
    if (!disk_output_active(&s->opts)) {
        return true;
    }
    if (!n1g_disk_save(s, s->opts.disk_out_path)) {
        n1g_info(s, "failed to save disk %s", s->opts.disk_out_path);
        return false;
    }
    return true;
}

static n1g_opts_t make_restart_opts(const n1g_state_t *s, const char *preset) {
    n1g_opts_t next = s->opts;
    apply_run_preset(&next, preset);
    bool same_seed_path = next.disk_path && next.disk_seed_path &&
                          strcmp(next.disk_path, next.disk_seed_path) == 0;
    bool same_seed_label = next.run_label && next.disk_seed_label &&
                           strcmp(next.run_label, next.disk_seed_label) == 0;
    if (next.disk_out_path && (same_seed_path || same_seed_label)) {
        next.disk_path = next.disk_out_path;
    }
    return next;
}

static void apply_run_preset(n1g_opts_t *opts, const char *preset) {
    const bool web_enabled = opts->web_enabled;
    const uint16_t web_port = opts->web_port;
    const bool web_no_hold = opts->web_no_hold;
    const char *ppm_path = opts->ppm_path;
    const bool run_forever = opts->run_forever;
    const bool trace_pc = opts->trace_pc;
    const bool trace_mmio = opts->trace_mmio;
    const bool apple_diagnostics = opts->apple_diagnostics;
    const bool host_profile = opts->host_profile;
    const bool verbose = opts->verbose;
    const char *disk_out_path = opts->disk_out_path;
    const char *disk_seed_path = opts->disk_seed_path;
    const char *disk_seed_label = opts->disk_seed_label;

    memset(opts, 0, sizeof(*opts));
    opts->boot_mode = N1G_BOOT_DIRECT;
    opts->dump_count = 1;
    opts->web_enabled = web_enabled;
    opts->web_port = web_port;
    opts->web_no_hold = web_no_hold;
    opts->run_forever = run_forever;
    opts->ppm_path = ppm_path;
    opts->trace_pc = trace_pc;
    opts->trace_mmio = trace_mmio;
    opts->apple_diagnostics = apple_diagnostics;
    opts->host_profile = host_profile;
    opts->verbose = verbose;
    opts->disk_out_path = disk_out_path;
    opts->disk_seed_path = disk_seed_path;
    opts->disk_seed_label = disk_seed_label;
    opts->rtc_usec_per_tick = 1;
    opts->timer_divider = 20;
    opts->load_addr = N1G_SDRAM_BASE;
    opts->battery_percent = 100u;

    if (strcmp(preset, "apple-native") == 0 || strcmp(preset, "apple-stage0") == 0) {
        opts->run_label = apple_stage0_label;
        opts->profile = N1G_PROFILE_APPLE;
        opts->firmware_path = apple_stage0_fw;
        opts->disk_path = apple_disk;
        opts->max_insns = 175000000u;
        opts->slice_insns = 512;
        opts->timer_divider = 1;
        opts->rtc_usec_per_tick = 8;
        opts->load_addr = N1G_FASTRAM_BASE;
        opts->entry = N1G_FASTRAM_BASE;
        opts->entry_set = true;
    } else if (strcmp(preset, "apple-direct") == 0) {
        opts->run_label = apple_direct_label;
        opts->profile = N1G_PROFILE_APPLE;
        opts->firmware_path = apple_direct_fw;
        opts->disk_path = apple_disk;
        opts->max_insns = 175000000u;
        opts->slice_insns = 128;
    } else if (strcmp(preset, "apple-official") == 0 || strcmp(preset, "apple-flash") == 0) {
        opts->run_label = apple_flash_label;
        opts->profile = N1G_PROFILE_APPLE;
        opts->boot_mode = N1G_BOOT_FLASH;
        opts->flash_path = apple_flash_rom_path();
        opts->disk_path = apple_disk;
        opts->max_insns = 175000000u;
        opts->slice_insns = 512;
        opts->timer_divider = 1;
        opts->rtc_usec_per_tick = 8;
        opts->virtual_memmap = true;
    } else if (strcmp(preset, "ipodlinux") == 0) {
        opts->run_label = ipodlinux_label;
        opts->profile = N1G_PROFILE_ROCKBOX;
        opts->firmware_path = ipodlinux_fw;
        opts->disk_path = ipodlinux_disk;
        opts->max_insns = 20000000u;
        opts->slice_insns = 512;
        opts->timer_divider = 1;
        opts->load_addr = N1G_SDRAM_BASE;
        opts->entry = N1G_SDRAM_BASE;
        opts->entry_set = true;
    } else {
        opts->run_label = rockbox_label;
        opts->profile = N1G_PROFILE_ROCKBOX;
        opts->firmware_path = rockbox_fw;
        opts->disk_path = rockbox_disk;
        opts->max_insns = 20000000u;
        opts->slice_insns = 512;
        opts->timer_divider = 1;
    }
}

static bool init_state(n1g_state_t *s, n1g_opts_t opts) {
    memset(s, 0, sizeof(*s));
    infer_run_label(&opts);
    s->opts = opts;
    memset(s->flash.bytes, 0xff, sizeof(s->flash.bytes));

    if (s->opts.input_script) {
        if (!n1g_input_script_load(&s->input_script_state, s->opts.input_script)) {
            n1g_log(s, "invalid --input script: %s", s->opts.input_script);
            return false;
        }
        n1g_info(s, "input script loaded: %s", s->opts.input_script);
    }
    if (!n1g_ram_init(s)) {
        n1g_die("failed to allocate RAM");
    }
    n1g_dev_evp_init(s);
    if (!n1g_disk_load(s, s->opts.disk_path)) {
        destroy_state(s);
        return false;
    }
    if (!n1g_load_flash_rom(s)) {
        destroy_state(s);
        return false;
    }
    if (!n1g_cpu_init(s)) {
        destroy_state(s);
        return false;
    }
    if (!n1g_boot_reset(s)) {
        destroy_state(s);
        return false;
    }
    if (!n1g_cpu_map_memory(s)) {
        destroy_state(s);
        return false;
    }
    return true;
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
    n1g_web_server_t web;
    memset(&s, 0, sizeof(s));
    memset(&web, 0, sizeof(web));
    n1g_opts_t opts = parse_args(argc, argv);
    infer_run_label(&opts);
    if (opts.disk_out_path) {
        opts.disk_seed_path = opts.disk_path;
        opts.disk_seed_label = opts.run_label;
    }
    signal(SIGINT, handle_stop_signal);
#ifdef SIGTERM
    signal(SIGTERM, handle_stop_signal);
#endif

    if (!init_state(&s, opts)) {
        return 1;
    }
    bool state_active = true;
    if (s.opts.web_enabled && !n1g_web_start(&s, &web, s.opts.web_port)) {
        destroy_state(&s);
        return 1;
    }

    int exit_code = 0;
    bool restart_now = false;
    char restart_preset[32] = {0};

run_image:
    do {
        restart_now = false;
        restart_preset[0] = '\0';
        n1g_host_profile_t host_profile;
        memset(&host_profile, 0, sizeof(host_profile));
        if (s.opts.host_profile) {
            host_profile.started_ns = host_profile_now_ns();
        }
        n1g_info(&s, "start image=%s max_insns=%llu slice_insns=%u timer_divider=%u rtc_usec_per_tick=%u run_forever=%u map_flash_zero=%u virtual_memmap=%u",
                 s.opts.run_label ? s.opts.run_label : "custom",
                 (unsigned long long)s.opts.max_insns,
                 s.opts.slice_insns,
                 s.opts.timer_divider,
                 s.opts.rtc_usec_per_tick,
                 s.opts.run_forever ? 1u : 0u,
                 s.opts.map_flash_zero ? 1u : 0u,
                 s.opts.virtual_memmap ? 1u : 0u);
        uint64_t remaining = s.opts.max_insns;
        while ((s.opts.run_forever || remaining > 0) && !stop_requested) {
            uint32_t slice = s.opts.slice_insns;
            if (!s.opts.run_forever && remaining < slice) {
                slice = (uint32_t)remaining;
            }
            uint64_t part_started_ns = s.opts.host_profile ? host_profile_now_ns() : 0;
            bool cpu_ok = n1g_cpu_step_slice(&s, N1G_CORE_CPU, slice);
            if (s.opts.host_profile) {
                host_profile_add(&host_profile.cpu_ns, part_started_ns);
                host_profile.cpu_calls++;
            }
            if (!cpu_ok) {
                break;
            }
            if (!s.cpu[N1G_CORE_COP].halted) {
                part_started_ns = s.opts.host_profile ? host_profile_now_ns() : 0;
                bool cop_ok = n1g_cpu_step_slice(&s, N1G_CORE_COP, slice);
                if (s.opts.host_profile) {
                    host_profile_add(&host_profile.cop_ns, part_started_ns);
                    host_profile.cop_calls++;
                }
                if (!cop_ok) {
                    break;
                }
            }
            part_started_ns = s.opts.host_profile ? host_profile_now_ns() : 0;
            n1g_bus_tick(&s);
            if (s.opts.host_profile) {
                host_profile_add(&host_profile.bus_ns, part_started_ns);
            }
            if (s.opts.input_script) {
                part_started_ns = s.opts.host_profile ? host_profile_now_ns() : 0;
                n1g_input_script_tick(&s);
                if (s.opts.host_profile) {
                    host_profile_add(&host_profile.input_ns, part_started_ns);
                }
            }
            if (s.opts.host_profile) {
                host_profile.slices++;
            }
            if (!s.opts.run_forever) {
                remaining -= slice;
            }
            if (s.opts.web_enabled && (s.counters.device_ticks & 0xffu) == 0) {
                part_started_ns = s.opts.host_profile ? host_profile_now_ns() : 0;
                n1g_web_poll(&s, &web, true);
                if (n1g_web_take_restart(&web, restart_preset, sizeof(restart_preset))) {
                    restart_now = true;
                    if (s.opts.host_profile) {
                        host_profile_add(&host_profile.web_ns, part_started_ns);
                        host_profile.web_polls++;
                    }
                    break;
                }
                if (s.opts.host_profile) {
                    host_profile_add(&host_profile.web_ns, part_started_ns);
                    host_profile.web_polls++;
                }
            }
        }
        if (s.opts.web_enabled) {
            uint64_t part_started_ns = s.opts.host_profile ? host_profile_now_ns() : 0;
            n1g_web_poll(&s, &web, false);
            if (!restart_now && n1g_web_take_restart(&web, restart_preset, sizeof(restart_preset))) {
                restart_now = true;
            }
            if (s.opts.host_profile) {
                host_profile_add(&host_profile.web_ns, part_started_ns);
                host_profile.web_polls++;
            }
        }
        if (s.opts.host_profile) {
            host_profile_report(&s, &host_profile);
        }

        if (!restart_now) {
            break;
        }

        n1g_info(&s, "web restart requested preset=%s", restart_preset);
        if (!save_disk_output(&s)) {
            exit_code = 1;
            break;
        }
        n1g_opts_t next = make_restart_opts(&s, restart_preset);
        destroy_state(&s);
        state_active = false;
        if (!init_state(&s, next)) {
            exit_code = 1;
            break;
        }
        state_active = true;
    } while (!stop_requested);

    if (!state_active) {
        n1g_web_stop(&web);
        return exit_code;
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
             "summary guest_insns=%llu ticks=%llu mmio_r=%llu mmio_w=%llu lcd_words=%llu lcd_gram=%llu lcd_block=%llu lcd_overruns=%llu dma_lcd_transfers=%llu disk_reads=%llu disk_writes=%llu irq=%llu pc=0x%08x i2s_tx=%llu i2s_drained=%llu dma_audio_starts=%llu dma_audio_done=%llu dma_audio_bytes=%llu",
             (unsigned long long)s.counters.guest_insns,
             (unsigned long long)s.counters.device_ticks,
             (unsigned long long)s.counters.mmio_reads,
             (unsigned long long)s.counters.mmio_writes,
             (unsigned long long)s.counters.lcd_words,
             (unsigned long long)s.lcd2.gram_pixels,
             (unsigned long long)s.lcd2.block_pixels,
             (unsigned long long)s.lcd2.block_overrun_words,
             (unsigned long long)(s.dma.lcd_transfers[0] + s.dma.lcd_transfers[1] +
                                  s.dma.lcd_transfers[2] + s.dma.lcd_transfers[3]),
             (unsigned long long)s.counters.disk_reads,
             (unsigned long long)s.counters.disk_writes,
             (unsigned long long)s.counters.irq_count,
             n1g_cpu_pc(&s, N1G_CORE_CPU),
             (unsigned long long)s.i2s.tx_halfwords,
             (unsigned long long)s.i2s.tx_drained_halfwords,
             (unsigned long long)(s.dma.ch[0].starts + s.dma.ch[1].starts + s.dma.ch[2].starts + s.dma.ch[3].starts),
             (unsigned long long)(s.dma.ch[0].completions + s.dma.ch[1].completions + s.dma.ch[2].completions + s.dma.ch[3].completions),
             (unsigned long long)(s.dma.ch[0].bytes_pushed + s.dma.ch[1].bytes_pushed + s.dma.ch[2].bytes_pushed + s.dma.ch[3].bytes_pushed));
    n1g_info(&s,
             "audio_output enabled=%u rate=%u stream=%u pcm=%llu nonzero=%llu silenced=%llu peak=%u underruns=%llu underrun_samples=%llu overruns=%llu dropped=%llu",
             s.i2c.wm8975_output_enabled ? 1u : 0u,
             s.i2s.pcm_sample_rate != 0u ? s.i2s.pcm_sample_rate : 44100u,
             s.i2s.pcm_stream_id,
             (unsigned long long)s.i2s.pcm_produced_halfwords,
             (unsigned long long)s.i2s.pcm_nonzero_halfwords,
             (unsigned long long)s.i2s.pcm_silenced_halfwords,
             s.i2s.pcm_peak,
             (unsigned long long)s.i2s.underruns,
             (unsigned long long)s.i2s.underrun_halfwords,
             (unsigned long long)s.i2s.tx_overruns,
             (unsigned long long)s.i2s.host_dropped_halfwords);
    if (s.opts.verbose) {
        for (uint32_t addr = 0; addr < 128u; addr++) {
            if (s.i2c.addr_reads[addr] != 0u || s.i2c.addr_writes[addr] != 0u) {
                n1g_info(&s,
                         "i2c_summary addr=0x%02x reads=%llu writes=%llu",
                         addr,
                         (unsigned long long)s.i2c.addr_reads[addr],
                         (unsigned long long)s.i2c.addr_writes[addr]);
            }
        }
        if (s.i2c.addr_writes[0x1au] != 0u) {
            n1g_info(&s,
                     "wm8975_state writes=%llu resets=%llu mode=%s output=%u muted=%u sample_rate=%u interface=0x%03x control_rate=0x%03x power=0x%03x out1=0x%03x/0x%03x",
                     (unsigned long long)s.i2c.addr_writes[0x1au],
                     (unsigned long long)s.i2c.wm8975_resets,
                     s.i2c.wm8975_legacy_mode ? "legacy" : "native",
                     s.i2c.wm8975_output_enabled ? 1u : 0u,
                     (s.i2c.wm8975_regs[0x05u] & (1u << 3u)) != 0u ? 1u : 0u,
                     s.i2c.wm8975_sample_rate,
                     (unsigned)s.i2c.wm8975_regs[0x07u],
                     (unsigned)s.i2c.wm8975_regs[0x08u],
                     (unsigned)s.i2c.wm8975_regs[s.i2c.wm8975_legacy_mode ? 0x06u : 0x1au],
                     (unsigned)s.i2c.wm8975_regs[0x02u],
                     (unsigned)s.i2c.wm8975_regs[0x03u]);
        }
    }
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
    n1g_info(&s,
             "lcd_state window=%u,%u-%u,%u cursor=%u,%u block_starts=%llu last_block_cursor=%u,%u cursor_sets=%llu window_sets=%llu",
             s.lcd2.window_x0,
             s.lcd2.window_y0,
             s.lcd2.window_x1,
             s.lcd2.window_y1,
             s.lcd2.cursor_x,
             s.lcd2.cursor_y,
             (unsigned long long)s.lcd2.block_starts,
             s.lcd2.last_block_cursor_x,
             s.lcd2.last_block_cursor_y,
             (unsigned long long)s.lcd2.cursor_sets,
             (unsigned long long)s.lcd2.window_sets);
    if (s.opts.profile == N1G_PROFILE_APPLE) {
        uint32_t active = read32_or_zero(&s, 0x10705b48u);
        uint32_t queue_slot = read32_or_zero(&s, 0x10743194u);
        n1g_info(&s,
                "apple_pc_hist plateau_24dxx=%llu event_2a=%llu ui_483xx=%llu timers_99dxx=%llu mp3_c8cxx=%llu event_cb_1526xx=%llu task_1bdxx=%llu fs_1c42xx=%llu other_fw=%llu other=%llu",
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
        if (s.counters.apple_handoff_seen) {
            const bool handoff_ok =
                s.counters.apple_handoff_tag == 0x53797349u &&
                s.counters.apple_handoff_sysinfo_ram;
            n1g_info(&s,
                    "apple_handoff status=%s pc=0x%08x slot=0x%08x tag=0x%08x sysinfo=0x%08x sysinfo_ram=%u sysinfo_e0=0x%08x sysinfo_e4=0x%08x",
                    handoff_ok ? "ok" : "missing-native-boot-metadata",
                    s.counters.apple_handoff_pc,
                    s.counters.apple_handoff_slot,
                    s.counters.apple_handoff_tag,
                    s.counters.apple_handoff_sysinfo,
                    s.counters.apple_handoff_sysinfo_ram ? 1u : 0u,
                    s.counters.apple_handoff_sysinfo_e0,
                    s.counters.apple_handoff_sysinfo_e4);
        }
        n1g_info(&s,
                "apple_input_hits isr_raw_1c6538=%llu isr_store_1c6574=%llu wake_2a058=%llu evq_post_b3468=%llu evq_got_b3508=%llu ui_qread_d0bb4=%llu lang_loop_4ee20=%llu lang_event_4ee44=%llu lang_scroll_4ee58=%llu lang_accept_4eec8=%llu select_24db4=%llu lang_post_4eeb4=%llu",
                (unsigned long long)s.counters.apple_input_hits[0],
                (unsigned long long)s.counters.apple_input_hits[1],
                (unsigned long long)s.counters.apple_input_hits[2],
                (unsigned long long)s.counters.apple_input_hits[3],
                (unsigned long long)s.counters.apple_input_hits[4],
                (unsigned long long)s.counters.apple_input_hits[5],
                (unsigned long long)s.counters.apple_input_hits[6],
                (unsigned long long)s.counters.apple_input_hits[7],
                (unsigned long long)s.counters.apple_input_hits[8],
                (unsigned long long)s.counters.apple_input_hits[9],
                (unsigned long long)s.counters.apple_input_hits[10],
                (unsigned long long)s.counters.apple_input_hits[11]);
        n1g_info(&s,
                "apple_input_last isr_raw=r0:0x%08x r1:0x%08x raw:0x%08x isr_store=buttons:0x%08x wheel:0x%08x state:0x%08x wake=r0:0x%08x r1:0x%08x lr:0x%08x lang_event=r4:0x%08x kind:0x%08x w28:0x%08x w30:0x%08x accept=r4:0x%08x kind:0x%08x w28:0x%08x w30:0x%08x select=r4:0x%08x kind:0x%08x w28:0x%08x w30:0x%08x",
                s.counters.apple_input_last[0][0],
                s.counters.apple_input_last[0][1],
                s.counters.apple_input_last[0][7],
                s.counters.apple_input_last[1][5],
                s.counters.apple_input_last[1][6],
                s.counters.apple_input_last[1][7],
                s.counters.apple_input_last[2][0],
                s.counters.apple_input_last[2][1],
                s.counters.apple_input_last[2][6],
                s.counters.apple_input_last[7][4],
                s.counters.apple_input_last[7][5],
                s.counters.apple_input_last[7][6],
                s.counters.apple_input_last[7][7],
                s.counters.apple_input_last[9][4],
                s.counters.apple_input_last[9][5],
                s.counters.apple_input_last[9][6],
                s.counters.apple_input_last[9][7],
                s.counters.apple_input_last[10][4],
                s.counters.apple_input_last[10][5],
                s.counters.apple_input_last[10][6],
                s.counters.apple_input_last[10][7]);
        n1g_info(&s,
                "apple_input_task_hits task_entry_1caa50=%llu wait_1caa7c=%llu woke_1caa84=%llu keypost_2fd0c=%llu gate1_2fd60=%llu gate2_2fd70=%llu gate3_2fd84=%llu gate4_2fd9c=%llu keypost_send_2fda8=%llu evq_dispatch_b32b8=%llu evq_handler_b32e4=%llu evq_ret_b32ec=%llu evq_forward_b330c=%llu msg_post_48060=%llu queue_send_32840=%llu ui_dispatch_d0c58=%llu",
                (unsigned long long)s.counters.apple_input_task_hits[0],
                (unsigned long long)s.counters.apple_input_task_hits[1],
                (unsigned long long)s.counters.apple_input_task_hits[2],
                (unsigned long long)s.counters.apple_input_task_hits[3],
                (unsigned long long)s.counters.apple_input_task_hits[4],
                (unsigned long long)s.counters.apple_input_task_hits[5],
                (unsigned long long)s.counters.apple_input_task_hits[6],
                (unsigned long long)s.counters.apple_input_task_hits[7],
                (unsigned long long)s.counters.apple_input_task_hits[8],
                (unsigned long long)s.counters.apple_input_task_hits[9],
                (unsigned long long)s.counters.apple_input_task_hits[10],
                (unsigned long long)s.counters.apple_input_task_hits[11],
                (unsigned long long)s.counters.apple_input_task_hits[12],
                (unsigned long long)s.counters.apple_input_task_hits[13],
                (unsigned long long)s.counters.apple_input_task_hits[14],
                (unsigned long long)s.counters.apple_input_task_hits[15]);
        n1g_info(&s,
                "apple_input_task_last wait=r0:0x%08x id:0x%08x woke=r0:0x%08x id:0x%08x keypost=r0:0x%08x r1:0x%08x send=r0:0x%08x r1:0x%08x queue=0x%08x payload=0x%08x p0=0x%08x p1=0x%08x ui_evt=0x%08x kind=0x%08x code=0x%08x",
                s.counters.apple_input_task_last[1][0],
                s.counters.apple_input_task_last[1][1],
                s.counters.apple_input_task_last[2][0],
                s.counters.apple_input_task_last[2][1],
                s.counters.apple_input_task_last[3][0],
                s.counters.apple_input_task_last[3][1],
                s.counters.apple_input_task_last[8][0],
                s.counters.apple_input_task_last[8][1],
                s.counters.apple_input_task_last[14][0],
                s.counters.apple_input_task_last[14][1],
                s.counters.apple_input_task_last[14][6],
                s.counters.apple_input_task_last[14][7],
                s.counters.apple_input_task_last[15][5],
                s.counters.apple_input_task_last[15][6],
                s.counters.apple_input_task_last[15][7]);
        n1g_info(&s,
                "apple_key_gate writes=%llu last_pc=0x%08x addr=0x%08x size=%u value=0x%08x r0=0x%08x r1=0x%08x lr=0x%08x bytes68=0x%08x bytes6c=0x%08x",
                (unsigned long long)s.counters.apple_key_gate_writes,
                s.counters.apple_key_gate_last[0],
                s.counters.apple_key_gate_last[1],
                s.counters.apple_key_gate_last[2],
                s.counters.apple_key_gate_last[3],
                s.counters.apple_key_gate_last[4],
                s.counters.apple_key_gate_last[5],
                s.counters.apple_key_gate_last[6],
                s.counters.apple_key_gate_last[7],
                s.counters.apple_key_gate_bytes);
        n1g_info(&s,
                "apple_ui_ready hits_483b8=%llu entry_4a404=%llu precall_4a410=%llu store_4a41c=%llu post_4a420=%llu bytes68=0x%08x bytes6c=0x%08x store_lr=0x%08x entry_lr=0x%08x",
                (unsigned long long)s.counters.apple_ui_ready_hits[0],
                (unsigned long long)s.counters.apple_ui_ready_hits[1],
                (unsigned long long)s.counters.apple_ui_ready_hits[2],
                (unsigned long long)s.counters.apple_ui_ready_hits[3],
                (unsigned long long)s.counters.apple_ui_ready_hits[4],
                s.counters.apple_ui_ready_bytes68,
                s.counters.apple_ui_ready_bytes6c,
                s.counters.apple_ui_ready_last[3][7],
                s.counters.apple_ui_ready_last[1][7]);
        n1g_info(&s,
                "apple_work_pool hits_b310c=%llu alloc_835b4=%llu gate_d0c54=%llu dispatch_d0c58=%llu pass_d0c5c=%llu guard_d0c68=%llu handler_load_d0c78=%llu handler_bx_d0c90=%llu head=0x%08x words=0x%08x,0x%08x,0x%08x,0x%08x branch_obj=0x%08x stale_r1=0x%08x dispatch_r4=0x%08x handler=0x%08x dispatch_lr=0x%08x",
                (unsigned long long)s.counters.apple_work_pool_hits[0],
                (unsigned long long)s.counters.apple_work_pool_hits[1],
                (unsigned long long)s.counters.apple_work_pool_hits[2],
                (unsigned long long)s.counters.apple_work_pool_hits[3],
                (unsigned long long)s.counters.apple_work_pool_hits[4],
                (unsigned long long)s.counters.apple_work_pool_hits[5],
                (unsigned long long)s.counters.apple_work_pool_hits[6],
                (unsigned long long)s.counters.apple_work_pool_hits[7],
                s.counters.apple_work_pool_head,
                s.counters.apple_work_pool_words[0],
                s.counters.apple_work_pool_words[1],
                s.counters.apple_work_pool_words[2],
                s.counters.apple_work_pool_words[3],
                s.counters.apple_work_pool_last[3][0],
                s.counters.apple_work_pool_last[3][1],
                s.counters.apple_work_pool_last[3][4],
                s.counters.apple_work_pool_last[7][3],
                s.counters.apple_work_pool_last[3][7]);
        n1g_info(&s,
                "apple_ui_branch hits_4ec94=%llu load_4eca0=%llu dispatch_25398=%llu select_24db4=%llu accept_24e20=%llu tail_24f08=%llu lang_accept_4eec8=%llu lang_event_4ee44=%llu obj=0x%08x obj_words=0x%08x,0x%08x,0x%08x,0x%08x dispatch_r0=0x%08x dispatch_r4=0x%08x select_kind=0x%08x select_w30=0x%08x accept_kind=0x%08x accept_w30=0x%08x",
                (unsigned long long)s.counters.apple_ui_branch_hits[0],
                (unsigned long long)s.counters.apple_ui_branch_hits[1],
                (unsigned long long)s.counters.apple_ui_branch_hits[2],
                (unsigned long long)s.counters.apple_ui_branch_hits[3],
                (unsigned long long)s.counters.apple_ui_branch_hits[4],
                (unsigned long long)s.counters.apple_ui_branch_hits[5],
                (unsigned long long)s.counters.apple_ui_branch_hits[6],
                (unsigned long long)s.counters.apple_ui_branch_hits[7],
                s.counters.apple_ui_branch_last[0][0],
                s.counters.apple_ui_branch_words[0][0],
                s.counters.apple_ui_branch_words[0][1],
                s.counters.apple_ui_branch_words[0][2],
                s.counters.apple_ui_branch_words[0][3],
                s.counters.apple_ui_branch_last[2][0],
                s.counters.apple_ui_branch_last[2][4],
                s.counters.apple_ui_branch_words[3][3],
                s.counters.apple_ui_branch_last[3][3],
                s.counters.apple_ui_branch_words[4][3],
                s.counters.apple_ui_branch_last[4][3]);
        n1g_info(&s,
                "apple_ui_dispatch post_2539c=%llu loaded_253a0=%llu branch_253a4=%llu objtab_2a8b0=%llu cmp_2a8d4=%llu found_2a8ec=%llu clear_2a91c=%llu post_2a944=%llu obj=0x%08x obj_words=0x%08x,0x%08x,0x%08x,0x%08x vt=0x%08x,0x%08x,0x%08x,0x%08x objtab_r0=0x%08x objtab_r1=0x%08x objtab_lr=0x%08x",
                (unsigned long long)s.counters.apple_ui_dispatch_hits[0],
                (unsigned long long)s.counters.apple_ui_dispatch_hits[1],
                (unsigned long long)s.counters.apple_ui_dispatch_hits[2],
                (unsigned long long)s.counters.apple_ui_dispatch_hits[3],
                (unsigned long long)s.counters.apple_ui_dispatch_hits[4],
                (unsigned long long)s.counters.apple_ui_dispatch_hits[5],
                (unsigned long long)s.counters.apple_ui_dispatch_hits[6],
                (unsigned long long)s.counters.apple_ui_dispatch_hits[7],
                s.counters.apple_ui_dispatch_last[2][0],
                s.counters.apple_ui_dispatch_words[2][0],
                s.counters.apple_ui_dispatch_words[2][1],
                s.counters.apple_ui_dispatch_words[2][2],
                s.counters.apple_ui_dispatch_words[2][3],
                s.counters.apple_ui_dispatch_words[2][4],
                s.counters.apple_ui_dispatch_words[2][5],
                s.counters.apple_ui_dispatch_words[2][6],
                s.counters.apple_ui_dispatch_words[2][7],
                s.counters.apple_ui_dispatch_last[3][0],
                s.counters.apple_ui_dispatch_last[3][1],
                s.counters.apple_ui_dispatch_last[3][7]);
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
                "apple_lcd_path_hits q_wait_53b20=%llu q_after_53b28=%llu q_cmp_53b30=%llu q_empty_53b34=%llu submit_53b38=%llu link_255a4=%llu link_after_255b0=%llu link_store_255b4=%llu lookup_25274=%llu dirty_4b74c=%llu wait_45dfc=%llu reset_45e6c=%llu",
                (unsigned long long)s.counters.apple_lcd_path_hits[0],
                (unsigned long long)s.counters.apple_lcd_path_hits[1],
                (unsigned long long)s.counters.apple_lcd_path_hits[2],
                (unsigned long long)s.counters.apple_lcd_path_hits[3],
                (unsigned long long)s.counters.apple_lcd_path_hits[4],
                (unsigned long long)s.counters.apple_lcd_path_hits[5],
                (unsigned long long)s.counters.apple_lcd_path_hits[6],
                (unsigned long long)s.counters.apple_lcd_path_hits[7],
                (unsigned long long)s.counters.apple_lcd_path_hits[8],
                (unsigned long long)s.counters.apple_lcd_path_hits[9],
                (unsigned long long)s.counters.apple_lcd_path_hits[10],
                (unsigned long long)s.counters.apple_lcd_path_hits[11]);
        n1g_info(&s,
                "apple_lcd_path_last q_after_53b28=r0:0x%08x r1:0x%08x r2:0x%08x r3:0x%08x r4:0x%08x lr:0x%08x q_cmp_53b30=r0:0x%08x r1:0x%08x r2:0x%08x r3:0x%08x r4:0x%08x lr:0x%08x link_after_255b0=r0:0x%08x r1:0x%08x r2:0x%08x r3:0x%08x r4:0x%08x lr:0x%08x link_store_255b4=r0:0x%08x r1:0x%08x r2:0x%08x r3:0x%08x r4:0x%08x lr:0x%08x active_plus8=0x%08x",
                s.counters.apple_lcd_path_last[1][0],
                s.counters.apple_lcd_path_last[1][1],
                s.counters.apple_lcd_path_last[1][2],
                s.counters.apple_lcd_path_last[1][3],
                s.counters.apple_lcd_path_last[1][4],
                s.counters.apple_lcd_path_last[1][5],
                s.counters.apple_lcd_path_last[2][0],
                s.counters.apple_lcd_path_last[2][1],
                s.counters.apple_lcd_path_last[2][2],
                s.counters.apple_lcd_path_last[2][3],
                s.counters.apple_lcd_path_last[2][4],
                s.counters.apple_lcd_path_last[2][5],
                s.counters.apple_lcd_path_last[6][0],
                s.counters.apple_lcd_path_last[6][1],
                s.counters.apple_lcd_path_last[6][2],
                s.counters.apple_lcd_path_last[6][3],
                s.counters.apple_lcd_path_last[6][4],
                s.counters.apple_lcd_path_last[6][5],
                s.counters.apple_lcd_path_last[7][0],
                s.counters.apple_lcd_path_last[7][1],
                s.counters.apple_lcd_path_last[7][2],
                s.counters.apple_lcd_path_last[7][3],
                s.counters.apple_lcd_path_last[7][4],
                s.counters.apple_lcd_path_last[7][5],
                read32_or_zero(&s, active + 8u));
        n1g_info(&s,
                "apple_lcd_producer_hits enter_540a0=%llu pre_54104=%llu call_54108=%llu deliver_5410c=%llu enqueue_a_54194=%llu enqueue_b_541ac=%llu branch_541cc=%llu store_541d4=%llu tail_541dc=%llu post_54208=%llu exit_54210=%llu",
                (unsigned long long)s.counters.apple_lcd_producer_hits[0],
                (unsigned long long)s.counters.apple_lcd_producer_hits[1],
                (unsigned long long)s.counters.apple_lcd_producer_hits[2],
                (unsigned long long)s.counters.apple_lcd_producer_hits[3],
                (unsigned long long)s.counters.apple_lcd_producer_hits[4],
                (unsigned long long)s.counters.apple_lcd_producer_hits[5],
                (unsigned long long)s.counters.apple_lcd_producer_hits[6],
                (unsigned long long)s.counters.apple_lcd_producer_hits[7],
                (unsigned long long)s.counters.apple_lcd_producer_hits[8],
                (unsigned long long)s.counters.apple_lcd_producer_hits[9],
                (unsigned long long)s.counters.apple_lcd_producer_hits[10]);
        n1g_info(&s,
                "apple_lcd_producer_last deliver_5410c=r0:0x%08x r1:0x%08x r2:0x%08x r3:0x%08x r4:0x%08x r5:0x%08x sp:0x%08x lr:0x%08x enqueue_a_54194=r0:0x%08x r1:0x%08x r2:0x%08x r3:0x%08x r4:0x%08x r5:0x%08x sp:0x%08x lr:0x%08x tail_541dc=r0:0x%08x r1:0x%08x r2:0x%08x r3:0x%08x r4:0x%08x r5:0x%08x sp:0x%08x lr:0x%08x",
                s.counters.apple_lcd_producer_last[3][0],
                s.counters.apple_lcd_producer_last[3][1],
                s.counters.apple_lcd_producer_last[3][2],
                s.counters.apple_lcd_producer_last[3][3],
                s.counters.apple_lcd_producer_last[3][4],
                s.counters.apple_lcd_producer_last[3][5],
                s.counters.apple_lcd_producer_last[3][6],
                s.counters.apple_lcd_producer_last[3][7],
                s.counters.apple_lcd_producer_last[4][0],
                s.counters.apple_lcd_producer_last[4][1],
                s.counters.apple_lcd_producer_last[4][2],
                s.counters.apple_lcd_producer_last[4][3],
                s.counters.apple_lcd_producer_last[4][4],
                s.counters.apple_lcd_producer_last[4][5],
                s.counters.apple_lcd_producer_last[4][6],
                s.counters.apple_lcd_producer_last[4][7],
                s.counters.apple_lcd_producer_last[8][0],
                s.counters.apple_lcd_producer_last[8][1],
                s.counters.apple_lcd_producer_last[8][2],
                s.counters.apple_lcd_producer_last[8][3],
                s.counters.apple_lcd_producer_last[8][4],
                s.counters.apple_lcd_producer_last[8][5],
                s.counters.apple_lcd_producer_last[8][6],
                s.counters.apple_lcd_producer_last[8][7]);
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

    if (s.opts.web_enabled && !s.opts.web_no_hold && !stop_requested) {
        n1g_info(&s, "web frontend holding final frame; press Ctrl+C to exit");
        while (!stop_requested) {
            n1g_web_poll(&s, &web, false);
            if (n1g_web_take_restart(&web, restart_preset, sizeof(restart_preset))) {
                n1g_info(&s, "web restart requested preset=%s", restart_preset);
                if (!save_disk_output(&s)) {
                    exit_code = 1;
                    break;
                }
                n1g_opts_t next = make_restart_opts(&s, restart_preset);
                destroy_state(&s);
                state_active = false;
                if (!init_state(&s, next)) {
                    exit_code = 1;
                    break;
                }
                state_active = true;
                goto run_image;
            }
            n1g_web_sleep_ms(16);
        }
    }
    if (!save_disk_output(&s)) {
        exit_code = 1;
    }
    n1g_web_stop(&web);
    if (state_active) {
        destroy_state(&s);
    }
    return exit_code;
}
