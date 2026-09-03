// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CRAU_NBD_NBD_SERVER_HPP
#define CRAU_NBD_NBD_SERVER_HPP


#include "source_provider.hpp"
#include "crau_parser.hpp"
#include "extent_index.hpp"
#include "block_cache.hpp"
#include "decompressor.hpp"

#include <string>
#include <memory>
#include <atomic>

namespace crau_nbd {

class NbdServer {
public:
    NbdServer(const std::string &device_path,
              ISourceReader &reader,
              const PartitionInfo &partition,
              uint64_t data_offset,
              size_t cache_size_mb,
              bool verbose);
    ~NbdServer();

    // Start server, run until stop requested
    bool run();

    // Request asynchronous stop
    void request_stop();

    // Static helper to detach / disconnect an existing NBD device
    static bool detach(const std::string &device_path);

private:
    bool handle_read(int sock_fd, uint64_t from, uint32_t len, const char *handle);
    bool read_blocks(uint64_t from, uint32_t len, uint8_t *out);

    std::string device_path_;
    ISourceReader &reader_;
    const PartitionInfo &partition_;
    uint64_t data_offset_;
    bool verbose_;

    ExtentIndex extent_index_;
    BlockCache block_cache_;
    Decompressor decompressor_;

    std::atomic<bool> stop_requested_{false};
};

} // namespace crau_nbd

#endif // CRAU_NBD_NBD_SERVER_HPP
