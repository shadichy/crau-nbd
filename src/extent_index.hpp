// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CRAU_NBD_EXTENT_INDEX_HPP
#define CRAU_NBD_EXTENT_INDEX_HPP


#include "update_metadata.pb.h"

#include <cstdint>
#include <vector>

namespace crau_nbd {

struct ExtentMapping {
    uint64_t start_block;
    uint64_t num_blocks;
    uint32_t op_index;
    uint64_t block_offset_in_op; // Offset in blocks within the decompressed op buffer
};

class ExtentIndex {
public:
    ExtentIndex();
    ~ExtentIndex();

    void build(const chromeos_update_engine::PartitionUpdate &partition, uint32_t block_size);

    // Look up which operation covers the logical block
    // Returns true if found and populates out parameters
    bool lookup(uint64_t block, uint32_t &op_index, uint64_t &block_offset_in_op) const;

    uint64_t total_blocks() const { return total_blocks_; }

private:
    std::vector<ExtentMapping> entries_;
    uint64_t total_blocks_;
    uint32_t block_size_;
};

} // namespace crau_nbd

#endif // CRAU_NBD_EXTENT_INDEX_HPP
