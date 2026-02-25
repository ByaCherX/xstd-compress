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
// Compression codec type. 4 bits (0-15).
// ---------------------------------------------------------------------------
enum class CompressionType : uint8_t {
    UNCOMPRESSED = 0,
    GZIP         = 1,
    BROTLI       = 2,
    LZ4          = 3,
    ZSTD         = 4,
};

// ---------------------------------------------------------------------------
// Compression effort level. 4 bits (0-15).
// ---------------------------------------------------------------------------
enum class CompressionLevel : uint8_t {
    FAST    = 0,
    DEFAULT = 1,
    BEST    = 2,
};

// ---------------------------------------------------------------------------
// CompressionCodec — packed into a single byte:
//   bits [7:4] = CompressionType
//   bits [3:0] = CompressionLevel
// ---------------------------------------------------------------------------
struct CompressionCodec {
    uint8_t raw{0};

    constexpr CompressionCodec() = default;

    constexpr CompressionCodec(CompressionType type, CompressionLevel level) noexcept
        : raw(static_cast<uint8_t>(
              (static_cast<uint8_t>(type)  << 4) |
              (static_cast<uint8_t>(level) & 0x0F))) {}

    [[nodiscard]] constexpr CompressionType  Type()  const noexcept {
        return static_cast<CompressionType>(raw >> 4);
    }
    [[nodiscard]] constexpr CompressionLevel Level() const noexcept {
        return static_cast<CompressionLevel>(raw & 0x0F);
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
