// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CRAU_NBD_BLOCK_CACHE_HPP
#define CRAU_NBD_BLOCK_CACHE_HPP


#include <cstdint>
#include <vector>
#include <list>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace crau_nbd {

class BlockCache {
public:
    explicit BlockCache(size_t max_bytes);
    ~BlockCache();

    // Look up cached operation data
    std::shared_ptr<const std::vector<uint8_t>> get(uint32_t op_index);

    // Store decompressed operation data
    void put(uint32_t op_index, std::shared_ptr<std::vector<uint8_t>> data);

    size_t current_size() const;
    size_t max_size() const;

private:
    struct CacheItem {
        uint32_t op_index;
        std::shared_ptr<std::vector<uint8_t>> data;
    };

    mutable std::mutex mutex_;
    size_t max_bytes_;
    size_t current_bytes_;
    std::list<CacheItem> lru_list_;
    std::unordered_map<uint32_t, std::list<CacheItem>::iterator> map_;
};

} // namespace crau_nbd

#endif // CRAU_NBD_BLOCK_CACHE_HPP
