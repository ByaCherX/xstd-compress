#include "page_manager.h"
#include "xstd_errors.h"

#include <bit>
#include <stdexcept>
#include <string>

namespace xstd {

PageManager::PageManager(int32_t initial_capacity) {
    if (initial_capacity < 0)
        XSTD_THROW_ERROR_MSG(kInvalidArgument, "PageManager capacity must be >= 0");
    Resize(initial_capacity);
}

int32_t PageManager::Allocate() {
    for (std::size_t word = 0; word < bitmap_.size(); ++word) {
        if (bitmap_[word] != ~uint64_t{0}) {
            int bit = CountTrailingOnes(bitmap_[word]);
            bitmap_[word] |= (uint64_t{1} << bit);
            int32_t id = static_cast<int32_t>(word * 64 + bit);
            if (id >= page_count_) page_count_ = id + 1;
            return id;
        }
    }
    // All words full — grow by 64 slots.
    int32_t id = static_cast<int32_t>(bitmap_.size() * 64);
    bitmap_.push_back(uint64_t{1});
    page_count_ = id + 1;
    return id;
}

void PageManager::AllocateAt(int32_t id) {
    if (id < 0) XSTD_THROW_ERROR_MSG(kInvalidArgument, "page id must be >= 0");
    EnsureCapacity(id + 1);
    std::size_t word = static_cast<std::size_t>(id) / 64;
    int         bit  = id % 64;
    if (bitmap_[word] & (uint64_t{1} << bit))
        XSTD_THROW_ERROR_MSG(kGENERIC, "page already allocated: " + std::to_string(id));
    bitmap_[word] |= (uint64_t{1} << bit);
    if (id >= page_count_) page_count_ = id + 1;
}

void PageManager::Free(int32_t id) noexcept {
    if (id < 0) return;
    std::size_t word = static_cast<std::size_t>(id) / 64;
    if (word >= bitmap_.size()) return;
    bitmap_[word] &= ~(uint64_t{1} << (id % 64));
}

bool PageManager::IsAllocated(int32_t id) const noexcept {
    if (id < 0) return false;
    std::size_t word = static_cast<std::size_t>(id) / 64;
    if (word >= bitmap_.size()) return false;
    return (bitmap_[word] >> (id % 64)) & 1u;
}

int32_t PageManager::AllocatedCount() const noexcept {
    int32_t count = 0;
    for (uint64_t w : bitmap_)
        count += static_cast<int32_t>(std::popcount(w));
    return count;
}

std::vector<uint8_t> PageManager::Serialise() const {
    std::vector<uint8_t> out(bitmap_.size() * sizeof(uint64_t));
    for (std::size_t i = 0; i < bitmap_.size(); ++i) {
        uint64_t w = bitmap_[i];
        for (int b = 0; b < 8; ++b)
            out[i * 8 + b] = static_cast<uint8_t>(w >> (b * 8));
    }
    return out;
}

void PageManager::Deserialise(std::span<const uint8_t> data) {
    if (data.size() % 8 != 0)
        XSTD_THROW_ERROR_MSG(kInvalidArgument, "PageManager bitmap data size must be a multiple of 8");
    bitmap_.resize(data.size() / 8);
    for (std::size_t i = 0; i < bitmap_.size(); ++i) {
        uint64_t w = 0;
        for (int b = 0; b < 8; ++b)
            w |= static_cast<uint64_t>(data[i * 8 + b]) << (b * 8);
        bitmap_[i] = w;
    }
    // Recompute page_count_ from last allocated bit.
    page_count_ = 0;
    for (int32_t i = static_cast<int32_t>(bitmap_.size()) - 1; i >= 0; --i) {
        if (bitmap_[i] != 0) {
            int msb = 63 - std::countl_zero(bitmap_[i]);
            page_count_ = static_cast<int32_t>(i) * 64 + msb + 1;
            break;
        }
    }
}

void PageManager::Resize(int32_t min_capacity) {
    std::size_t words = (static_cast<std::size_t>(min_capacity) + 63) / 64;
    bitmap_.assign(words, 0);
}

void PageManager::EnsureCapacity(int32_t needed) {
    std::size_t words = (static_cast<std::size_t>(needed) + 63) / 64;
    if (words > bitmap_.size()) bitmap_.resize(words, 0);
}

int PageManager::CountTrailingOnes(uint64_t w) noexcept {
    return std::countr_zero(~w);
}

} // namespace xstd
