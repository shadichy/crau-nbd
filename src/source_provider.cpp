// SPDX-License-Identifier: GPL-3.0-or-later
#include "source_provider.hpp"
#include "error.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <cerrno>
#include <vector>
#include <algorithm>

namespace crau_nbd {

ssize_t posix_pread_all(int fd, void *buf, size_t count, off_t offset) {
    size_t total = 0;
    char *p = static_cast<char *>(buf);
    while (total < count) {
        ssize_t n = ::pread(fd, p + total, count - total, offset + total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            break; // EOF
        }
        total += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(total);
}

class RawFileSourceReader : public ISourceReader {
public:
    RawFileSourceReader(int fd, std::string path, uint64_t size, off_t base_offset)
        : fd_(fd), path_(std::move(path)), size_(size), base_offset_(base_offset) {}

    ~RawFileSourceReader() override {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    ssize_t read_at(void *buf, size_t count, off_t offset) override {
        if (static_cast<uint64_t>(offset) >= size_) {
            return 0;
        }
        size_t to_read = count;
        if (static_cast<uint64_t>(offset + count) > size_) {
            to_read = static_cast<size_t>(size_ - offset);
        }
        return posix_pread_all(fd_, buf, to_read, base_offset_ + offset);
    }

    uint64_t size() const override { return size_; }
    const std::string &path() const override { return path_; }
    int fd() const override { return fd_; }

private:
    int fd_;
    std::string path_;
    uint64_t size_;
    off_t base_offset_;
};

// Inspects ZIP archive to locate payload.bin
static std::unique_ptr<ISourceReader> open_zip(int fd, const std::string &path, uint64_t file_size) {
    // Locate End of Central Directory Record (EOCD)
    // EOCD signature: 0x06054b50 ("PK\x05\x06")
    size_t search_len = std::min<size_t>(file_size, 65536 + 22);
    std::vector<uint8_t> tail(search_len);
    off_t tail_offset = static_cast<off_t>(file_size - search_len);
    if (posix_pread_all(fd, tail.data(), search_len, tail_offset) != static_cast<ssize_t>(search_len)) {
        gnu_error("failed to read end of zip archive '%s'", path.c_str());
        return nullptr;
    }

    off_t eocd_pos = -1;
    for (ssize_t i = static_cast<ssize_t>(search_len) - 22; i >= 0; --i) {
        if (tail[i] == 0x50 && tail[i + 1] == 0x4b && tail[i + 2] == 0x05 && tail[i + 3] == 0x06) {
            eocd_pos = tail_offset + i;
            break;
        }
    }

    uint64_t cd_offset = 0;
    uint64_t cd_size = 0;
    uint32_t total_entries = 0; (void)total_entries;

    if (eocd_pos >= 0) {
        size_t rel = static_cast<size_t>(eocd_pos - tail_offset);
        total_entries = tail[rel + 10] | (tail[rel + 11] << 8);
        cd_size = tail[rel + 12] | (tail[rel + 13] << 8) | (tail[rel + 14] << 16) | (tail[rel + 15] << 24);
        cd_offset = tail[rel + 16] | (tail[rel + 17] << 8) | (tail[rel + 18] << 16) | (tail[rel + 19] << 24);
    }

    // Iterate Central Directory entries
    bool found_payload = false;
    uint16_t compression_method = 0;
    uint64_t uncompressed_size = 0; (void)uncompressed_size;
    uint64_t local_header_offset = 0;

    if (cd_offset > 0 && cd_size > 0 && cd_offset + cd_size <= file_size) {
        std::vector<uint8_t> cd(cd_size);
        if (posix_pread_all(fd, cd.data(), cd_size, static_cast<off_t>(cd_offset)) == static_cast<ssize_t>(cd_size)) {
            size_t idx = 0;
            while (idx + 46 <= cd_size) {
                if (cd[idx] != 0x50 || cd[idx + 1] != 0x4b || cd[idx + 2] != 0x01 || cd[idx + 3] != 0x02) {
                    break;
                }
                uint16_t method = cd[idx + 10] | (cd[idx + 11] << 8);
                uint32_t unc_sz = cd[idx + 24] | (cd[idx + 25] << 8) | (cd[idx + 26] << 16) | (cd[idx + 27] << 24);
                uint16_t name_len = cd[idx + 28] | (cd[idx + 29] << 8);
                uint16_t extra_len = cd[idx + 30] | (cd[idx + 31] << 8);
                uint16_t comment_len = cd[idx + 32] | (cd[idx + 33] << 8);
                uint32_t loc_hdr_off = cd[idx + 42] | (cd[idx + 43] << 8) | (cd[idx + 44] << 16) | (cd[idx + 45] << 24);

                if (idx + 46 + name_len <= cd_size) {
                    std::string fname(reinterpret_cast<char *>(&cd[idx + 46]), name_len);
                    if (fname == "payload.bin") {
                        found_payload = true;
                        compression_method = method;
                        uncompressed_size = unc_sz;
                        local_header_offset = loc_hdr_off;
                        break;
                    }
                }
                idx += 46 + name_len + extra_len + comment_len;
            }
        }
    }

    // Fallback: scan local file headers directly from start of file
    if (!found_payload) {
        off_t cur = 0;
        uint8_t lhdr[30];
        while (cur + 30 <= static_cast<off_t>(file_size)) {
            if (posix_pread_all(fd, lhdr, 30, cur) != 30) break;
            if (lhdr[0] != 0x50 || lhdr[1] != 0x4b || lhdr[2] != 0x03 || lhdr[3] != 0x04) break;

            uint16_t method = lhdr[8] | (lhdr[9] << 8);
            uint32_t comp_sz = lhdr[18] | (lhdr[19] << 8) | (lhdr[20] << 16) | (lhdr[21] << 24);
            uint32_t unc_sz = lhdr[22] | (lhdr[23] << 8) | (lhdr[24] << 16) | (lhdr[25] << 24);
            uint16_t name_len = lhdr[26] | (lhdr[27] << 8);
            uint16_t extra_len = lhdr[28] | (lhdr[29] << 8);

            std::vector<char> name_buf(name_len);
            if (posix_pread_all(fd, name_buf.data(), name_len, cur + 30) == name_len) {
                std::string fname(name_buf.data(), name_len);
                if (fname == "payload.bin") {
                    found_payload = true;
                    compression_method = method;
                    uncompressed_size = unc_sz;
                    local_header_offset = cur;
                    break;
                }
            }
            cur += 30 + name_len + extra_len + comp_sz;
        }
    }

    if (!found_payload) {
        gnu_error("no 'payload.bin' found inside zip archive '%s'", path.c_str());
        return nullptr;
    }

    // Strict Deflate Policy
    if (compression_method != 0) {
        gnu_error("'payload.bin' inside archive is compressed (method %u != STORED)", compression_method);
        gnu_note("deflated ZIP archives do not support random O(1) block seeking");
        gnu_note("mount the archive first using 'mount-zip' and supply the virtual payload:");
        std::fprintf(stderr, "    mount-zip %s /mnt/zip\n", path.c_str());
        std::fprintf(stderr, "    pkexec crau-nbd attach -d /dev/nbd0 -p <partition> /mnt/zip/payload.bin\n");
        return nullptr;
    }

    // Read local header to get payload data offset
    uint8_t lhdr[30];
    if (posix_pread_all(fd, lhdr, 30, static_cast<off_t>(local_header_offset)) != 30) {
        gnu_error("failed to read local file header for payload.bin in '%s'", path.c_str());
        return nullptr;
    }
    uint16_t name_len = lhdr[26] | (lhdr[27] << 8);
    uint16_t extra_len = lhdr[28] | (lhdr[29] << 8);
    off_t data_offset = static_cast<off_t>(local_header_offset) + 30 + name_len + extra_len;

    // Read payload size from payload header if uncompressed_size was 0 or ZIP64
    char magic[4];
    if (posix_pread_all(fd, magic, 4, data_offset) != 4 || std::memcmp(magic, "CrAU", 4) != 0) {
        gnu_error("payload.bin inside '%s' does not start with CrAU magic", path.c_str());
        return nullptr;
    }

    return std::make_unique<RawFileSourceReader>(fd, path, file_size - data_offset, data_offset);
}

std::unique_ptr<ISourceReader> open_source(const std::string &path) {
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        gnu_posix_error(path.c_str());
        return nullptr;
    }

    struct stat st;
    if (::fstat(fd, &st) < 0) {
        gnu_posix_error("fstat");
        ::close(fd);
        return nullptr;
    }

    uint8_t magic[4];
    if (posix_pread_all(fd, magic, 4, 0) != 4) {
        gnu_error("failed to read file header from '%s'", path.c_str());
        ::close(fd);
        return nullptr;
    }

    // If starts with "CrAU", it's a raw payload.bin
    if (std::memcmp(magic, "CrAU", 4) == 0) {
        return std::make_unique<RawFileSourceReader>(fd, path, static_cast<uint64_t>(st.st_size), 0);
    }

    // If starts with "PK\x03\x04", it's a ZIP archive
    if (magic[0] == 0x50 && magic[1] == 0x4b && magic[2] == 0x03 && magic[3] == 0x04) {
        return open_zip(fd, path, static_cast<uint64_t>(st.st_size));
    }

    gnu_error("unrecognized file format for '%s' (expected 'CrAU' or ZIP archive)", path.c_str());
    ::close(fd);
    return nullptr;
}

} // namespace crau_nbd
