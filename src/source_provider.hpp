// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CRAU_NBD_SOURCE_PROVIDER_HPP
#define CRAU_NBD_SOURCE_PROVIDER_HPP


#include <cstdint>
#include <memory>
#include <string>
#include <sys/types.h>

namespace crau_nbd {

// POSIX thread-safe pread wrapper handling EINTR and short reads
ssize_t posix_pread_all(int fd, void *buf, size_t count, off_t offset);

class ISourceReader {
public:
    virtual ~ISourceReader() = default;

    // Read count bytes at offset inside the payload
    virtual ssize_t read_at(void *buf, size_t count, off_t offset) = 0;

    // Total size of payload
    virtual uint64_t size() const = 0;

    // Original file path
    virtual const std::string &path() const = 0;

    // Underlying file descriptor
    virtual int fd() const = 0;
};

// Factory to open either raw payload.bin or a stored ZIP archive
std::unique_ptr<ISourceReader> open_source(const std::string &path);

} // namespace crau_nbd

#endif // CRAU_NBD_SOURCE_PROVIDER_HPP
