// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CRAU_NBD_ERROR_HPP
#define CRAU_NBD_ERROR_HPP


#include <cstdarg>
#include <cerrno>
#include <string>

namespace crau_nbd {

// Set program name for GNU error messages
void set_program_name(const char *name);
const char *get_program_name();

// GNU Coding Standards 4.9: Error Messages
// Format: "program_name: error: message"
void gnu_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// GNU note / informational diagnostic
void gnu_note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// GNU warning diagnostic
void gnu_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Reports POSIX system error with strerror(errno)
void gnu_posix_error(const char *action);

} // namespace crau_nbd

#endif // CRAU_NBD_ERROR_HPP
