#pragma once

// Fast non-cryptographic block hashing via XXH64.

#include <cstddef>
#include <cstdint>

#define XXH_INLINE_ALL
#include "packages/xxhash/xxhash.h"

namespace xstd {

typedef uint64_t XXH64_hash_t;
typedef uint32_t XXH32_hash_t;

namespace XXHasher {
    static constexpr uint64_t kSeed = 0x78737464637263EEull;  // 'xstdcrc' in hex

    [[nodiscard]] static XXH64_hash_t Hash(const void* data, std::size_t length) noexcept {
        if (data == nullptr) return 0;
        if (length == 0) return 0;
        return XXH_INLINE_XXH64(data, length, kSeed);
    };
    [[nodiscard]] static XXH32_hash_t Hash32(const void* data, std::size_t length) noexcept {
        if (data == nullptr) return 0;
        if (length == 0) return 0;
        return XXH_INLINE_XXH32(data, length, (kSeed & 0xFFFFFFFFu));
    };
}

} // namespace xstd
