// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CRAU_NBD_CRAU_PARSER_HPP
#define CRAU_NBD_CRAU_PARSER_HPP


#include "source_provider.hpp"
#include "update_metadata.pb.h"

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace crau_nbd {

struct Extent {
    uint64_t start_block;
    uint64_t num_blocks;
};

struct PartitionInfo {
    std::string name;
    uint64_t size_bytes;
    uint64_t total_blocks;
    uint32_t block_size;
    const chromeos_update_engine::PartitionUpdate *partition_proto;
};

class CrauParser {
public:
    explicit CrauParser(ISourceReader &reader);
    ~CrauParser();

    bool parse();

    uint32_t block_size() const;
    uint64_t data_offset() const;
    const std::vector<PartitionInfo> &partitions() const;
    const PartitionInfo *find_partition(const std::string &name) const;

private:
    ISourceReader &reader_;
    uint64_t data_offset_;
    uint32_t block_size_;
    chromeos_update_engine::DeltaArchiveManifest manifest_;
    std::vector<PartitionInfo> partitions_;
};

} // namespace crau_nbd

#endif // CRAU_NBD_CRAU_PARSER_HPP
