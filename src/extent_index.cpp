// SPDX-License-Identifier: GPL-3.0-or-later
#include "extent_index.hpp"

#include <algorithm>

namespace crau_nbd {

ExtentIndex::ExtentIndex() : total_blocks_(0), block_size_(4096) {}
ExtentIndex::~ExtentIndex() = default;

void ExtentIndex::build(const chromeos_update_engine::PartitionUpdate &partition, uint32_t block_size) {
    block_size_ = block_size;
    entries_.clear();
    total_blocks_ = 0;

    for (int op_idx = 0; op_idx < partition.operations_size(); ++op_idx) {
        const auto &op = partition.operations(op_idx);
        uint64_t op_block_offset = 0;

        for (const auto &ext : op.dst_extents()) {
            ExtentMapping m;
            m.start_block = ext.start_block();
            m.num_blocks = ext.num_blocks();
            m.op_index = static_cast<uint32_t>(op_idx);
            m.block_offset_in_op = op_block_offset;

            entries_.push_back(m);
            op_block_offset += ext.num_blocks();

            uint64_t end = m.start_block + m.num_blocks;
            if (end > total_blocks_) {
                total_blocks_ = end;
            }
        }
    }

    // Sort entries by start_block
    std::sort(entries_.begin(), entries_.end(), [](const ExtentMapping &a, const ExtentMapping &b) {
        return a.start_block < b.start_block;
    });
}

bool ExtentIndex::lookup(uint64_t block, uint32_t &op_index, uint64_t &block_offset_in_op) const {
    if (entries_.empty() || block >= total_blocks_) {
        return false;
    }

    // Binary search for the first entry with start_block > block
    auto it = std::upper_bound(entries_.begin(), entries_.end(), block,
        [](uint64_t val, const ExtentMapping &m) {
            return val < m.start_block;
        });

    if (it == entries_.begin()) {
        return false; // Hole / sparse before first entry
    }

    --it;
    if (block >= it->start_block && block < it->start_block + it->num_blocks) {
        op_index = it->op_index;
        block_offset_in_op = it->block_offset_in_op + (block - it->start_block);
        return true;
    }

    return false; // Hole / sparse region
}

} // namespace crau_nbd
