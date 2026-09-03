// SPDX-License-Identifier: GPL-3.0-or-later
#include "error.hpp"
#include "source_provider.hpp"
#include "crau_parser.hpp"
#include "extent_index.hpp"
#include "block_cache.hpp"
#include "decompressor.hpp"

#include <iostream>
#include <cassert>
#include <cstring>

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <ota.zip | payload.bin>\n";
        return 1;
    }

    std::string path = argv[1];
    std::cout << "[TEST] Opening source: " << path << "\n";
    auto source = crau_nbd::open_source(path);
    assert(source != nullptr);

    std::cout << "[TEST] Parsing CrAU manifest...\n";
    crau_nbd::CrauParser parser(*source);
    assert(parser.parse());

    const auto &partitions = parser.partitions();
    std::cout << "[TEST] Discovered " << partitions.size() << " partitions.\n";
    assert(partitions.size() >= 4);

    // Test 1: Verify kernel partition
    const auto *kernel_part = parser.find_partition("kernel");
    assert(kernel_part != nullptr);
    std::cout << "[TEST] Testing 'kernel' partition (" << kernel_part->total_blocks << " blocks)...\n";

    crau_nbd::ExtentIndex kernel_index;
    kernel_index.build(*kernel_part->partition_proto, kernel_part->block_size);
    assert(kernel_index.total_blocks() == kernel_part->total_blocks);

    crau_nbd::Decompressor decompressor(*source, parser.data_offset(), parser.block_size());
    crau_nbd::BlockCache cache(64 * 1024 * 1024);

    // Read first block of kernel
    uint32_t op_idx = 0;
    uint64_t block_offset = 0;
    assert(kernel_index.lookup(0, op_idx, block_offset));

    const auto &op0 = kernel_part->partition_proto->operations(op_idx);
    auto decomp = decompressor.decompress_operation(op0);
    assert(decomp != nullptr);
    assert(decomp->size() >= kernel_part->block_size);

    // Check Linux kernel boot header magic at offset 0x202: "HdrS"
    if (decomp->size() >= 0x206) {
        const char *magic = reinterpret_cast<const char *>(decomp->data() + 0x202);
        std::cout << "[TEST] Kernel boot header magic at 0x202: "
                  << magic[0] << magic[1] << magic[2] << magic[3] << "\n";
        assert(std::memcmp(magic, "HdrS", 4) == 0);
        std::cout << "[TEST] Verified Linux kernel 'HdrS' magic successfully!\n";
    }

    // Test 2: Verify system partition (LZMA & BZ2 mixed)
    const auto *sys_part = parser.find_partition("system");
    assert(sys_part != nullptr);
    std::cout << "[TEST] Testing 'system' partition (" << sys_part->total_blocks
              << " blocks, ~" << (sys_part->size_bytes / (1024 * 1024)) << " MB)...\n";

    crau_nbd::ExtentIndex sys_index;
    sys_index.build(*sys_part->partition_proto, sys_part->block_size);

    // Sample read block 0 (superblock area)
    assert(sys_index.lookup(0, op_idx, block_offset));
    const auto &sys_op0 = sys_part->partition_proto->operations(op_idx);
    auto sys_decomp0 = decompressor.decompress_operation(sys_op0);
    assert(sys_decomp0 != nullptr);
    std::cout << "[TEST] Successfully decompressed system block 0 (op " << op_idx
              << ", type " << sys_op0.type() << ")!\n";

    // Sample read block 1000
    if (sys_index.total_blocks() > 1000) {
        assert(sys_index.lookup(1000, op_idx, block_offset));
        const auto &sys_op1000 = sys_part->partition_proto->operations(op_idx);
        auto sys_decomp1000 = decompressor.decompress_operation(sys_op1000);
        assert(sys_decomp1000 != nullptr);
        std::cout << "[TEST] Successfully decompressed system block 1000 (op " << op_idx
                  << ", type " << sys_op1000.type() << ")!\n";
    }

    std::cout << "[TEST] ALL AUTOMATED TESTS PASSED!\n";
    return 0;
}
