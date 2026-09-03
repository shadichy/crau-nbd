// SPDX-License-Identifier: GPL-3.0-or-later
#include "crau_parser.hpp"
#include "error.hpp"

#include <cstring>
#include <arpa/inet.h>
#include <algorithm>

namespace crau_nbd {

CrauParser::CrauParser(ISourceReader &reader)
    : reader_(reader), data_offset_(0), block_size_(4096) {}

CrauParser::~CrauParser() = default;

bool CrauParser::parse() {
    uint8_t hdr[24];
    if (reader_.read_at(hdr, 24, 0) != 24) {
        gnu_error("failed to read 24-byte CrAU header");
        return false;
    }

    if (std::memcmp(hdr, "CrAU", 4) != 0) {
        gnu_error("invalid magic in payload header (expected 'CrAU')");
        return false;
    }

    uint64_t major = 0;
    std::memcpy(&major, hdr + 4, 8);
    major = __builtin_bswap64(major);

    if (major != 2) {
        gnu_error("unsupported CrAU major version %llu (only version 2 is supported)",
                  static_cast<unsigned long long>(major));
        return false;
    }

    uint64_t manifest_size = 0;
    std::memcpy(&manifest_size, hdr + 12, 8);
    manifest_size = __builtin_bswap64(manifest_size);

    uint32_t sig_size = 0;
    std::memcpy(&sig_size, hdr + 20, 4);
    sig_size = __builtin_bswap32(sig_size);

    data_offset_ = 24 + manifest_size + sig_size;

    std::vector<uint8_t> manifest_data(manifest_size);
    if (reader_.read_at(manifest_data.data(), manifest_size, 24) != static_cast<ssize_t>(manifest_size)) {
        gnu_error("failed to read %llu bytes of manifest protobuf",
                  static_cast<unsigned long long>(manifest_size));
        return false;
    }

    if (!manifest_.ParseFromArray(manifest_data.data(), static_cast<int>(manifest_size))) {
        gnu_error("failed to parse DeltaArchiveManifest protobuf");
        return false;
    }

    block_size_ = manifest_.has_block_size() ? manifest_.block_size() : 4096;

    partitions_.clear();
    partitions_.reserve(manifest_.partitions_size());

    for (int i = 0; i < manifest_.partitions_size(); ++i) {
        const auto &p = manifest_.partitions(i);
        PartitionInfo info;
        info.name = p.partition_name();
        info.block_size = block_size_;
        info.partition_proto = &p;

        uint64_t max_block = 0;
        for (const auto &op : p.operations()) {
            for (const auto &ext : op.dst_extents()) {
                uint64_t end = ext.start_block() + ext.num_blocks();
                if (end > max_block) {
                    max_block = end;
                }
            }
        }
        info.total_blocks = max_block;
        info.size_bytes = max_block * block_size_;

        partitions_.push_back(info);
    }

    return true;
}

uint32_t CrauParser::block_size() const { return block_size_; }
uint64_t CrauParser::data_offset() const { return data_offset_; }
const std::vector<PartitionInfo> &CrauParser::partitions() const { return partitions_; }

const PartitionInfo *CrauParser::find_partition(const std::string &name) const {
    for (const auto &p : partitions_) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

} // namespace crau_nbd
