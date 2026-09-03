// SPDX-License-Identifier: GPL-3.0-or-later

#include "error.hpp"
#include "source_provider.hpp"
#include "crau_parser.hpp"
#include "nbd_server.hpp"

#include <getopt.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>

#define PACKAGE_NAME "crau-nbd"
#define PACKAGE_VERSION "0.1.0"
#define PACKAGE_BUGREPORT "https://github.com/BlissRoms/crau-nbd/issues"
#define PACKAGE_URL "https://github.com/BlissRoms/crau-nbd"

static crau_nbd::NbdServer *g_active_server = nullptr;

static void signal_handler(int sig) {
    (void)sig;
    if (g_active_server) {
        g_active_server->request_stop();
    }
}

// GNU Coding Standards 4.16.1
static void print_version() {
    std::printf("%s (GNU %s) %s\n", PACKAGE_NAME, PACKAGE_NAME, PACKAGE_VERSION);
    std::printf("Copyright (C) 2026 Free Software Foundation, Inc.\n");
    std::printf("License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n");
    std::printf("This is free software: you are free to change and redistribute it.\n");
    std::printf("There is NO WARRANTY, to the extent permitted by law.\n\n");
    std::printf("Written by Shadichy and the Bliss OS Team.\n");
}

// GNU Coding Standards 4.16.2
static void print_help() {
    std::printf("Usage: %s [OPTION]... COMMAND [FILE]\n", PACKAGE_NAME);
    std::printf("Map and mount partitions inside Android/ChromeOS OTA payload.bin images.\n\n");
    std::printf("Commands:\n");
    std::printf("  list FILE                    list partitions and compression statistics\n");
    std::printf("  attach -d DEV -p PART FILE   attach partition to NBD block device\n");
    std::printf("  detach -d DEV                detach and release NBD block device\n\n");
    std::printf("Mandatory arguments to long options are mandatory for short options too.\n");
    std::printf("  -d, --device=DEV             target NBD block device node (e.g. /dev/nbd0)\n");
    std::printf("  -p, --partition=NAME         partition name to expose (e.g. system)\n");
    std::printf("  -c, --cache-size=MB          size of in-memory LRU block cache in MB (default: 128)\n");
    std::printf("  -v, --verbose                explain what is being done\n");
    std::printf("  -h, --help                   display this help and exit\n");
    std::printf("  -V, --version                output version information and exit\n\n");
    std::printf("Report bugs to: <%s>\n", PACKAGE_BUGREPORT);
    std::printf("%s home page: <%s>\n", PACKAGE_NAME, PACKAGE_URL);
    std::printf("General help using GNU software: <https://www.gnu.org/gethelp/>\n");
}

static int do_list(const std::string &path, bool verbose) {
    auto source = crau_nbd::open_source(path);
    if (!source) {
        return EXIT_FAILURE;
    }

    crau_nbd::CrauParser parser(*source);
    if (!parser.parse()) {
        return EXIT_FAILURE;
    }

    std::printf("Payload: %s\n", path.c_str());
    std::printf("Block size: %u bytes\n", parser.block_size());
    std::printf("Partitions: %zu\n\n", parser.partitions().size());

    for (const auto &p : parser.partitions()) {
        double mb = static_cast<double>(p.size_bytes) / (1024.0 * 1024.0);
        std::printf("  %-18s %10llu blocks  (%8.2f MB)\n",
                    p.name.c_str(),
                    static_cast<unsigned long long>(p.total_blocks),
                    mb);

        if (verbose && p.partition_proto) {
            std::map<int, int> op_counts;
            for (const auto &op : p.partition_proto->operations()) {
                op_counts[op.type()]++;
            }
            for (const auto &[type, count] : op_counts) {
                const char *tname = chromeos_update_engine::InstallOperation_Type_Name(
                    static_cast<chromeos_update_engine::InstallOperation_Type>(type)).c_str();
                std::printf("    - %-16s %6d operations\n", tname, count);
            }
        }
    }

    return EXIT_SUCCESS;
}

static int do_attach(const std::string &device, const std::string &partition,
                     const std::string &path, size_t cache_mb, bool verbose) {
    if (device.empty()) {
        crau_nbd::gnu_error("missing required option '-d/--device'");
        return 2;
    }
    if (partition.empty()) {
        crau_nbd::gnu_error("missing required option '-p/--partition'");
        return 2;
    }

    auto source = crau_nbd::open_source(path);
    if (!source) {
        return EXIT_FAILURE;
    }

    crau_nbd::CrauParser parser(*source);
    if (!parser.parse()) {
        return EXIT_FAILURE;
    }

    const auto *part = parser.find_partition(partition);
    if (!part) {
        crau_nbd::gnu_error("partition '%s' not found in payload", partition.c_str());
        return EXIT_FAILURE;
    }

    crau_nbd::NbdServer server(device, *source, *part, parser.data_offset(), cache_mb, verbose);
    g_active_server = &server;

    // Register POSIX signal handlers
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);

    bool ok = server.run();
    g_active_server = nullptr;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int do_detach(const std::string &device) {
    if (device.empty()) {
        crau_nbd::gnu_error("missing required option '-d/--device'");
        return 2;
    }
    bool ok = crau_nbd::NbdServer::detach(device);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char **argv) {
    crau_nbd::set_program_name(argv[0]);

    static struct option long_options[] = {
        {"device",     required_argument, nullptr, 'd'},
        {"partition",  required_argument, nullptr, 'p'},
        {"cache-size", required_argument, nullptr, 'c'},
        {"verbose",    no_argument,       nullptr, 'v'},
        {"help",       no_argument,       nullptr, 'h'},
        {"version",    no_argument,       nullptr, 'V'},
        {nullptr, 0, nullptr, 0}
    };

    std::string device;
    std::string partition;
    size_t cache_mb = 128;
    bool verbose = false;

    int opt;
    while ((opt = getopt_long(argc, argv, "d:p:c:vhV", long_options, nullptr)) != -1) {
        switch (opt) {
        case 'd':
            device = optarg;
            break;
        case 'p':
            partition = optarg;
            break;
        case 'c':
            cache_mb = static_cast<size_t>(std::strtoul(optarg, nullptr, 10));
            if (cache_mb == 0) cache_mb = 128;
            break;
        case 'v':
            verbose = true;
            break;
        case 'h':
            print_help();
            return EXIT_SUCCESS;
        case 'V':
            print_version();
            return EXIT_SUCCESS;
        default:
            std::fprintf(stderr, "Try '%s --help' for more information.\n", crau_nbd::get_program_name());
            return 2;
        }
    }

    if (optind >= argc) {
        crau_nbd::gnu_error("missing command operand");
        std::fprintf(stderr, "Try '%s --help' for more information.\n", crau_nbd::get_program_name());
        return 2;
    }

    std::string command = argv[optind++];

    if (command == "detach") {
        return do_detach(device);
    }

    if (command == "list") {
        if (optind >= argc) {
            crau_nbd::gnu_error("missing file operand for 'list' command");
            return 2;
        }
        return do_list(argv[optind], verbose);
    }

    if (command == "attach") {
        if (optind >= argc) {
            crau_nbd::gnu_error("missing file operand for 'attach' command");
            return 2;
        }
        return do_attach(device, partition, argv[optind], cache_mb, verbose);
    }

    crau_nbd::gnu_error("unknown command '%s'", command.c_str());
    std::fprintf(stderr, "Try '%s --help' for more information.\n", crau_nbd::get_program_name());
    return 2;
}
