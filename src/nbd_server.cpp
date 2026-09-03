// SPDX-License-Identifier: GPL-3.0-or-later
#include "nbd_server.hpp"
#include "error.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <linux/nbd.h>
#include <arpa/inet.h>
#include <endian.h>
#include <poll.h>

#include <cstring>
#include <thread>
#include <vector>

namespace crau_nbd {

static ssize_t posix_write_all(int fd, const void *buf, size_t count) {
    size_t total = 0;
    const char *p = static_cast<const char *>(buf);
    while (total < count) {
        ssize_t n = ::write(fd, p + total, count - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        total += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(total);
}

static ssize_t posix_read_full(int fd, void *buf, size_t count) {
    size_t total = 0;
    char *p = static_cast<char *>(buf);
    while (total < count) {
        ssize_t n = ::read(fd, p + total, count - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        total += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(total);
}

NbdServer::NbdServer(const std::string &device_path,
                     ISourceReader &reader,
                     const PartitionInfo &partition,
                     uint64_t data_offset,
                     size_t cache_size_mb,
                     bool verbose)
    : device_path_(device_path),
      reader_(reader),
      partition_(partition),
      data_offset_(data_offset),
      verbose_(verbose),
      block_cache_(cache_size_mb * 1024ULL * 1024ULL),
      decompressor_(reader, data_offset, partition.block_size) {
    if (partition_.partition_proto) {
        extent_index_.build(*partition_.partition_proto, partition_.block_size);
    }
}

NbdServer::~NbdServer() {
    request_stop();
}

void NbdServer::request_stop() {
    stop_requested_.store(true, std::memory_order_relaxed);
}

bool NbdServer::detach(const std::string &device_path) {
    int fd = ::open(device_path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        gnu_posix_error(device_path.c_str());
        return false;
    }
    if (::ioctl(fd, NBD_DISCONNECT) < 0) {
        // May already be disconnected
    }
    ::ioctl(fd, NBD_CLEAR_SOCK);
    ::ioctl(fd, NBD_CLEAR_QUE);
    ::close(fd);
    return true;
}

bool NbdServer::read_blocks(uint64_t from, uint32_t len, uint8_t *out) {
    uint32_t bs = partition_.block_size;
    uint64_t cur_offset = from;
    uint32_t remaining = len;
    uint8_t *cur_out = out;

    while (remaining > 0) {
        uint64_t block_idx = cur_offset / bs;
        uint32_t block_offset = cur_offset % bs;
        uint32_t bytes_to_copy = std::min<uint32_t>(remaining, bs - block_offset);

        uint32_t op_index = 0;
        uint64_t block_offset_in_op = 0;

        if (extent_index_.lookup(block_idx, op_index, block_offset_in_op)) {
            auto cached = block_cache_.get(op_index);
            if (!cached) {
                const auto &op = partition_.partition_proto->operations(op_index);
                auto decompressed = decompressor_.decompress_operation(op);
                if (!decompressed) {
                    gnu_error("decompression failure on block %llu (op %u)",
                              static_cast<unsigned long long>(block_idx), op_index);
                    std::memset(cur_out, 0, bytes_to_copy);
                } else {
                    block_cache_.put(op_index, decompressed);
                    cached = decompressed;
                }
            }

            if (cached) {
                uint64_t src_byte_offset = block_offset_in_op * bs + block_offset;
                if (src_byte_offset + bytes_to_copy <= cached->size()) {
                    std::memcpy(cur_out, cached->data() + src_byte_offset, bytes_to_copy);
                } else {
                    std::memset(cur_out, 0, bytes_to_copy);
                }
            }
        } else {
            // Hole / zero
            std::memset(cur_out, 0, bytes_to_copy);
        }

        cur_offset += bytes_to_copy;
        cur_out += bytes_to_copy;
        remaining -= bytes_to_copy;
    }

    return true;
}

bool NbdServer::handle_read(int sock_fd, uint64_t from, uint32_t len, const char *handle) {
    std::vector<uint8_t> data(len);
    read_blocks(from, len, data.data());

    struct nbd_reply reply;
    reply.magic = htonl(NBD_REPLY_MAGIC);
    reply.error = 0;
    std::memcpy(reply.handle, handle, 8);

    if (posix_write_all(sock_fd, &reply, sizeof(reply)) != sizeof(reply)) {
        return false;
    }
    if (posix_write_all(sock_fd, data.data(), len) != static_cast<ssize_t>(len)) {
        return false;
    }
    return true;
}

bool NbdServer::run() {
    int nbd_fd = ::open(device_path_.c_str(), O_RDWR | O_CLOEXEC);
    if (nbd_fd < 0) {
        gnu_posix_error(device_path_.c_str());
        return false;
    }

    // Clean up any lingering sockets on the device
    ::ioctl(nbd_fd, NBD_CLEAR_SOCK);
    ::ioctl(nbd_fd, NBD_CLEAR_QUE);

    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) {
        gnu_posix_error("socketpair");
        ::close(nbd_fd);
        return false;
    }

    if (::ioctl(nbd_fd, NBD_SET_SOCK, sv[0]) < 0) {
        gnu_posix_error("ioctl(NBD_SET_SOCK)");
        ::close(sv[0]);
        ::close(sv[1]);
        ::close(nbd_fd);
        return false;
    }

    if (::ioctl(nbd_fd, NBD_SET_BLKSIZE, partition_.block_size) < 0) {
        gnu_posix_error("ioctl(NBD_SET_BLKSIZE)");
        ::close(sv[0]);
        ::close(sv[1]);
        ::close(nbd_fd);
        return false;
    }

    if (::ioctl(nbd_fd, NBD_SET_SIZE_BLOCKS, partition_.total_blocks) < 0) {
        gnu_posix_error("ioctl(NBD_SET_SIZE_BLOCKS)");
        ::close(sv[0]);
        ::close(sv[1]);
        ::close(nbd_fd);
        return false;
    }

    if (::ioctl(nbd_fd, NBD_SET_FLAGS, NBD_FLAG_READ_ONLY | NBD_FLAG_HAS_FLAGS) < 0) {
        gnu_posix_error("ioctl(NBD_SET_FLAGS)");
    }

    std::atomic<bool> kernel_done{false};
    std::thread kernel_thread([nbd_fd, &kernel_done]() {
        ::ioctl(nbd_fd, NBD_DO_IT);
        kernel_done.store(true);
    });

    if (verbose_) {
        std::fprintf(stderr, "%s: attached partition '%s' (%llu blocks, %llu MB) to %s\n",
                     get_program_name(),
                     partition_.name.c_str(),
                     static_cast<unsigned long long>(partition_.total_blocks),
                     static_cast<unsigned long long>(partition_.size_bytes / (1024 * 1024)),
                     device_path_.c_str());
    }

    // Main server loop on sv[1]
    int client_fd = sv[1];
    while (!stop_requested_.load(std::memory_order_relaxed)) {
        struct pollfd pfd;
        pfd.fd = client_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int pr = ::poll(&pfd, 1, 250); // 250ms tick to check stop_requested_
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) continue; // Timeout

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            break;
        }

        struct nbd_request req;
        ssize_t rd = posix_read_full(client_fd, &req, sizeof(req));
        if (rd <= 0) {
            break;
        }

        uint32_t magic = ntohl(req.magic);
        if (magic != NBD_REQUEST_MAGIC) {
            gnu_error("invalid NBD request magic 0x%08x", magic);
            break;
        }

        uint32_t type = ntohl(req.type);
        uint64_t from = be64toh(req.from);
        uint32_t len = ntohl(req.len);

        if (type == NBD_CMD_READ) {
            if (!handle_read(client_fd, from, len, req.handle)) {
                break;
            }
        } else if (type == NBD_CMD_WRITE) {
            // Drain write data
            std::vector<uint8_t> dummy(len);
            posix_read_full(client_fd, dummy.data(), len);

            struct nbd_reply reply;
            reply.magic = htonl(NBD_REPLY_MAGIC);
            reply.error = htonl(EROFS); // Read-only
            std::memcpy(reply.handle, req.handle, 8);
            posix_write_all(client_fd, &reply, sizeof(reply));
        } else if (type == NBD_CMD_DISC) {
            if (verbose_) {
                std::fprintf(stderr, "%s: received NBD_CMD_DISC\n", get_program_name());
            }
            break;
        } else {
            // Trim/Flush
            struct nbd_reply reply;
            reply.magic = htonl(NBD_REPLY_MAGIC);
            reply.error = 0;
            std::memcpy(reply.handle, req.handle, 8);
            posix_write_all(client_fd, &reply, sizeof(reply));
        }
    }

    // Teardown
    ::ioctl(nbd_fd, NBD_DISCONNECT);
    ::close(client_fd);
    ::close(sv[0]);

    if (kernel_thread.joinable()) {
        kernel_thread.join();
    }

    ::ioctl(nbd_fd, NBD_CLEAR_SOCK);
    ::ioctl(nbd_fd, NBD_CLEAR_QUE);
    ::close(nbd_fd);

    if (verbose_) {
        std::fprintf(stderr, "%s: disconnected and cleaned up %s\n", get_program_name(), device_path_.c_str());
    }

    return true;
}

} // namespace crau_nbd
