// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CRAU_NBD_DECOMPRESSOR_HPP
#define CRAU_NBD_DECOMPRESSOR_HPP


#include "source_provider.hpp"
#include "update_metadata.pb.h"

#include <cstdint>
#include <vector>
#include <memory>

namespace crau_nbd {

class Decompressor {
public:
    Decompressor(ISourceReader &reader, uint64_t data_offset, uint32_t block_size);
    ~Decompressor();

    // Decompresses the entire operation into a newly allocated buffer
    std::shared_ptr<std::vector<uint8_t>> decompress_operation(
        const chromeos_update_engine::InstallOperation &op);

private:
    bool decompress_xz(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size);
    bool decompress_bz2(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size);

    ISourceReader &reader_;
    uint64_t data_offset_;
    uint32_t block_size_;
};

} // namespace crau_nbd

#endif // CRAU_NBD_DECOMPRESSOR_HPP
