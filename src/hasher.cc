#include <array>
#include "hasher.h"

#define XXH_INLINE_ALL
#include "packages/xxhash/xxhash.h"

namespace xstd {

XXH64_hash_t XXHasher::Hash(const void* data, std::size_t length) const noexcept {
    if (data == nullptr) return static_cast<XXH64_hash_t>(0);
    if (length == 0) return static_cast<XXH64_hash_t>(0);
    return XXH_INLINE_XXH64(data, length, kSeed);
}

XXH32_hash_t XXHasher::Hash32(const void* data, std::size_t length) const noexcept {
    if (data == nullptr) return 0;
    if (length == 0) return 0;
    return XXH_INLINE_XXH32(data, length, kSeed);
}

} // namespace xstd
