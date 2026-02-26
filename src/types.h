#pragma once

#include <cstdint>

namespace xstd {

// ---------------------------------------------------------------------------
// Page size options. Actual byte values are resolved via PageSizeBytes().
// ---------------------------------------------------------------------------
enum class PageSize : uint8_t {
    PAGE_4K   = 0,
    PAGE_8K   = 1,
    PAGE_16K  = 2,
    PAGE_32K  = 3,
    PAGE_64K  = 4,
    PAGE_128K = 5,
    PAGE_256K = 6,
    PAGE_512K = 7,
    PAGE_1M   = 8,
    PAGE_4M   = 9,
};

/// Returns the byte size for a PageSize enum value.
constexpr uint32_t PageSizeBytes(PageSize ps) noexcept {
    constexpr uint32_t kTable[] = {
        4   << 10, // PAGE_4K
        8   << 10, // PAGE_8K
        16  << 10, // PAGE_16K
        32  << 10, // PAGE_32K
        64  << 10, // PAGE_64K
        128 << 10, // PAGE_128K
        256 << 10, // PAGE_256K
        512 << 10, // PAGE_512K
        1   << 20, // PAGE_1M
        4   << 20, // PAGE_4M
    };
    return kTable[static_cast<uint8_t>(ps)];
}

// ---------------------------------------------------------------------------
// Encryption algorithms supported by Xstd.
// ---------------------------------------------------------------------------
enum class EncryptionAlgorithm : uint8_t {
    NONE       = 0xFF, // No encryption
    AES_GCM_V1 = 0,    // AES-256-GCM  (authenticated)
    AES_CTR_V1 = 1,    // AES-256-CTR  (faster, no auth tag)
};

// ---------------------------------------------------------------------------
// AES key sizes.
// ---------------------------------------------------------------------------
enum class AesKeySize : uint8_t {
    AES_128 = 16,
    AES_192 = 24,
    AES_256 = 32,
};

// ---------------------------------------------------------------------------
// Encoding applied to raw page data BEFORE compression.
// ---------------------------------------------------------------------------
enum class Encoding : uint8_t {
    PLAIN                    = 0,
    RLE                      = 1,   // Run-Length Encoding
    DELTA                    = 2,   // Delta encoding (integer sequences)
    DELTA_LENGTH_BYTE_ARRAY  = 3,   // Delta-encoded lengths + raw bytes
    DELTA_BYTE_ARRAY         = 4,   // Delta prefix-encoded byte arrays
};

// ---------------------------------------------------------------------------
// Compression codec type. 3 bits (0-7).
// ---------------------------------------------------------------------------
enum class CompressionType : uint8_t {
    UNCOMPRESSED = 0,
    GZIP         = 1,
    BROTLI       = 2,
    LZ4          = 3,
    ZSTD         = 4,
    OPENZL       = 5,
    SNAPPY       = 6,
    RESERVED_3   = 7,
};

// ---------------------------------------------------------------------------
// Compression effort level. 4 bits (0-15).
// ---------------------------------------------------------------------------
enum class CompressionLevel : uint8_t {
    XSTD_RESERVED_LEVEL = 0, // Reserved for future use
    XSTD_fast           = 1,
    XSTD_dfast          = 2,
    XSTD_greedy         = 3,
    XSTD_lazy           = 4,
    XSTD_lazy2          = 5,
    XSTD_btlazy         = 6,
    XSTD_btopt          = 7,
    XSTD_bultra         = 8,
    XSTD_bmax           = 9,
    XSTD_RESERVED_LEVEL_15 = 15, // Reserved for future use
};

// ---------------------------------------------------------------------------
// Reserved bit in CompressionCodec byte.
// ---------------------------------------------------------------------------
enum class CompressionReserved : uint8_t {
    ZERO = 0, // Must be zero for forward compatibility
    ONE  = 1,
};

// ---------------------------------------------------------------------------
// CompressionCodec — packed into a single byte:
//   bit  [7]   = CompressionReserved (1 bit)
//   bits [6:4] = CompressionType     (3 bits)
//   bits [3:0] = CompressionLevel    (4 bits)
// ---------------------------------------------------------------------------
struct CompressionCodec {
    uint8_t raw{0};

    constexpr CompressionCodec() = default;

    constexpr CompressionCodec(CompressionType    type,
                               CompressionLevel   level,
                               CompressionReserved reserved = CompressionReserved::ZERO) noexcept
        : raw(static_cast<uint8_t>(
              (static_cast<uint8_t>(reserved)            << 7) |
              ((static_cast<uint8_t>(type)  & 0x07u)     << 4) |
              (static_cast<uint8_t>(level)  & 0x0Fu))) {}

    [[nodiscard]] constexpr CompressionReserved Reserved() const noexcept {
        return static_cast<CompressionReserved>(raw >> 7);
    }
    [[nodiscard]] constexpr CompressionType  Type()  const noexcept {
        return static_cast<CompressionType>((raw >> 4) & 0x07u);
    }
    [[nodiscard]] constexpr CompressionLevel Level() const noexcept {
        return static_cast<CompressionLevel>(raw & 0x0Fu);
    }
};

// ---------------------------------------------------------------------------
// Page types.
// ---------------------------------------------------------------------------
enum class PageType : uint8_t {
    DATA_PAGE       = 0,
    INDEX_PAGE      = 1,
    DICTIONARY_PAGE = 2,
};

} // namespace xstd
