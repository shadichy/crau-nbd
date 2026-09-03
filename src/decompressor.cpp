// SPDX-License-Identifier: GPL-3.0-or-later
#include "decompressor.hpp"
#include "error.hpp"

#include <lzma.h>
#include <bzlib.h>
#include <cstring>

namespace crau_nbd {

Decompressor::Decompressor(ISourceReader &reader, uint64_t data_offset, uint32_t block_size)
    : reader_(reader), data_offset_(data_offset), block_size_(block_size) {}

Decompressor::~Decompressor() = default;

bool Decompressor::decompress_xz(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size) {
    uint64_t memlimit = UINT64_MAX;
    size_t in_pos = 0;
    size_t out_pos = 0;
    lzma_ret ret = lzma_stream_buffer_decode(
        &memlimit, 0, nullptr,
        in, &in_pos, in_size,
        out, &out_pos, out_size);
    if (ret != LZMA_OK) {
        gnu_error("lzma decompression failed (ret = %d)", ret);
        return false;
    }
    return true;
}

bool Decompressor::decompress_bz2(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size) {
    unsigned int dest_len = static_cast<unsigned int>(out_size);
    int ret = BZ2_bzBuffToBuffDecompress(
        reinterpret_cast<char *>(out), &dest_len,
        reinterpret_cast<char *>(const_cast<uint8_t *>(in)),
        static_cast<unsigned int>(in_size),
        0, 0);
    if (ret != BZ_OK) {
        gnu_error("bz2 decompression failed (ret = %d)", ret);
        return false;
    }
    return true;
}

std::shared_ptr<std::vector<uint8_t>> Decompressor::decompress_operation(
    const chromeos_update_engine::InstallOperation &op) {

    uint64_t total_blocks = 0;
    for (const auto &ext : op.dst_extents()) {
        total_blocks += ext.num_blocks();
    }
    size_t expected_size = total_blocks * block_size_;
    auto result = std::make_shared<std::vector<uint8_t>>(expected_size);

    if (op.type() == chromeos_update_engine::InstallOperation_Type_ZERO) {
        std::memset(result->data(), 0, expected_size);
        return result;
    }

    // Read compressed or raw chunk from payload
    uint64_t raw_offset = data_offset_ + op.data_offset();
    uint64_t raw_len = op.data_length();

    if (op.type() == chromeos_update_engine::InstallOperation_Type_REPLACE) {
        if (reader_.read_at(result->data(), raw_len, raw_offset) != static_cast<ssize_t>(raw_len)) {
            gnu_error("failed to read %llu raw bytes from payload offset %llu",
                      static_cast<unsigned long long>(raw_len),
                      static_cast<unsigned long long>(raw_offset));
            return nullptr;
        }
        return result;
    }

    // Read compressed blob into memory
    std::vector<uint8_t> compressed(raw_len);
    if (reader_.read_at(compressed.data(), raw_len, raw_offset) != static_cast<ssize_t>(raw_len)) {
        gnu_error("failed to read %llu compressed bytes from payload offset %llu",
                  static_cast<unsigned long long>(raw_len),
                  static_cast<unsigned long long>(raw_offset));
        return nullptr;
    }

    if (op.type() == chromeos_update_engine::InstallOperation_Type_REPLACE_XZ) {
        if (!decompress_xz(compressed.data(), raw_len, result->data(), expected_size)) {
            return nullptr;
        }
        return result;
    }

    if (op.type() == chromeos_update_engine::InstallOperation_Type_REPLACE_BZ) {
        if (!decompress_bz2(compressed.data(), raw_len, result->data(), expected_size)) {
            return nullptr;
        }
        return result;
    }

    gnu_error("unsupported operation type %d", op.type());
    return nullptr;
}

} // namespace crau_nbd
