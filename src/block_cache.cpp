// SPDX-License-Identifier: GPL-3.0-or-later
#include "block_cache.hpp"

namespace crau_nbd {

BlockCache::BlockCache(size_t max_bytes)
    : max_bytes_(max_bytes), current_bytes_(0) {}

BlockCache::~BlockCache() = default;

std::shared_ptr<const std::vector<uint8_t>> BlockCache::get(uint32_t op_index) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = map_.find(op_index);
    if (it == map_.end()) {
        return nullptr;
    }
    // Move to front (most recently used)
    lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
    return it->second->data;
}

void BlockCache::put(uint32_t op_index, std::shared_ptr<std::vector<uint8_t>> data) {
    if (!data || data->empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);

    size_t item_size = data->size();

    // If already exists, update and move to front
    auto it = map_.find(op_index);
    if (it != map_.end()) {
        current_bytes_ -= it->second->data->size();
        it->second->data = data;
        current_bytes_ += item_size;
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
    } else {
        lru_list_.push_front({op_index, data});
        map_[op_index] = lru_list_.begin();
        current_bytes_ += item_size;
    }

    // Evict least recently used until within budget
    while (current_bytes_ > max_bytes_ && !lru_list_.empty()) {
        auto last = std::prev(lru_list_.end());
        current_bytes_ -= last->data->size();
        map_.erase(last->op_index);
        lru_list_.pop_back();
    }
}

size_t BlockCache::current_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_bytes_;
}

size_t BlockCache::max_size() const {
    return max_bytes_;
}

} // namespace crau_nbd
