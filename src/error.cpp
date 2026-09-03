// SPDX-License-Identifier: GPL-3.0-or-later
#include "error.hpp"

#include <cstdio>
#include <cstring>
#include <cerrno>

namespace crau_nbd {

static std::string g_program_name = "crau-nbd";

void set_program_name(const char *name) {
    if (!name) return;
    const char *p = strrchr(name, '/');
    g_program_name = p ? p + 1 : name;
}

const char *get_program_name() {
    return g_program_name.c_str();
}

void gnu_error(const char *fmt, ...) {
    std::fprintf(stderr, "%s: error: ", get_program_name());
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "\n");
}

void gnu_note(const char *fmt, ...) {
    std::fprintf(stderr, "%s: note: ", get_program_name());
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "\n");
}

void gnu_warn(const char *fmt, ...) {
    std::fprintf(stderr, "%s: warning: ", get_program_name());
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "\n");
}

void gnu_posix_error(const char *action) {
    int err = errno;
    std::fprintf(stderr, "%s: error: %s: %s\n", get_program_name(), action, std::strerror(err));
}

} // namespace crau_nbd
