#pragma once

// Fast non-cryptographic block hashing via XXH64.

#include <cstddef>
#include <cstdint>

namespace xstd {

class IHasher {
public:
    virtual ~IHasher() = default;
    [[nodiscard]] virtual uint64_t Hash(const void* data, std::size_t length) const noexcept = 0;
};

class XXHasher final : public IHasher {
public:
    [[nodiscard]] XXH64_hash_t Hash(const void* data, std::size_t length) const noexcept override;
    [[nodiscard]] XXH32_hash_t Hash32(const void* data, std::size_t length) const noexcept;
    static constexpr uint64_t kSeed = 0;
};

} // namespace xstd
