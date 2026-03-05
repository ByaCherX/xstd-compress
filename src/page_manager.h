#pragma once

// ---------------------------------------------------------------------------
// page_manager.h — Bitmap-based free-page allocator.
//
// One bit per page slot (0 = free, 1 = allocated).
// Internal storage: std::vector<uint64_t> (one word = 64 pages).
// Thread-safety is the responsibility of the caller.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <span>
#include <vector>

namespace xstd {

class PageManager {
public:
    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    /// Creates a PageManager pre-sized for @p initial_capacity page slots.
    explicit PageManager(int32_t initial_capacity = 1024);

    // -- Allocation --
    /// Returns the next free page ID and marks it as allocated.
    int32_t Allocate();
    /// Allocates a specific page ID. Throws if already allocated.
    void AllocateAt(int32_t id);
    /// Marks a page as free. No-op if already free.
    void Free(int32_t id) noexcept;

    // -- Query --
    [[nodiscard]] bool    IsAllocated(int32_t id)  const noexcept;
    [[nodiscard]] int32_t PageCount()              const noexcept { return page_count_; }
    [[nodiscard]] int32_t AllocatedCount()         const noexcept;

    // -- Serialisation --
    [[nodiscard]] std::vector<uint8_t> Serialise()  const;
    void Deserialise(std::span<const uint8_t> data);

private:
    std::vector<uint64_t> bitmap_;
    int32_t               page_count_{0};

    void Resize(int32_t min_capacity);
    void EnsureCapacity(int32_t needed);
    static int CountTrailingOnes(uint64_t w) noexcept;
};

} // namespace xstd
