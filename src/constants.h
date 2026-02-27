#pragma once

#include <cstdint>

namespace xstd {

// ---------------------------------------------------------------------------
// Magic numbers
// ---------------------------------------------------------------------------

/// 'XSTD' — first 4 bytes of every .xstd archive.
constexpr uint32_t kMagic       = 0x58535444u;  // 'X','S','T','D'

/// 'XEND' — last 4 bytes of every .xstd archive footer.
constexpr uint32_t kFooterMagic = 0x58454E44u;  // 'X','E','N','D'

// ---------------------------------------------------------------------------
// Versioning
// ---------------------------------------------------------------------------
constexpr int32_t kCurrentVersion  = 2;
constexpr int32_t kMinReadVersion  = 1;  // oldest version this code can read

// ---------------------------------------------------------------------------
// Structural sizes (bytes)
// ---------------------------------------------------------------------------

/// Size of the serialised ArchiveHeader on disk.
constexpr uint32_t kArchiveHeaderSize = 32;

/// Size of the serialised ArchiveFooter on disk.
constexpr uint32_t kArchiveFooterSize = 32;

/// Size of a serialised PageHeader on disk (fixed-length).
constexpr uint32_t kPageHeaderSize = 32;

// ---------------------------------------------------------------------------
// Crypto constants
// ---------------------------------------------------------------------------

/// GCM nonce / IV length in bytes (96-bit recommended by NIST SP 800-38D).
constexpr uint32_t kAesGcmIvSize   = 12;

/// GCM authentication tag length in bytes.
constexpr uint32_t kAesGcmTagSize  = 16;

/// CTR IV length in bytes (128-bit = one AES block).
constexpr uint32_t kAesCtrIvSize   = 16;

/// Maximum AES key size in bytes (AES-256).
constexpr uint32_t kMaxAesKeySize  = 32;

// ---------------------------------------------------------------------------
// Hashing / integrity
// ---------------------------------------------------------------------------

/// SHA-256 digest size in bytes.
constexpr uint32_t kSha256Size = 32;

/// xxHash seed used for CRC-style page integrity checks.
constexpr uint64_t kPageHashSeed = 0xDEADBEEFCAFEBABEull;

// ---------------------------------------------------------------------------
// B+ Tree defaults
// ---------------------------------------------------------------------------

/// Default order (max keys per node) for the file catalog B+ Tree.
constexpr int kBTreeDefaultOrder = 64;

// ---------------------------------------------------------------------------
// I/O defaults
// ---------------------------------------------------------------------------

/// Default page size for new archives.
constexpr uint32_t kDefaultPageSizeBytes = 64 << 10;  // 64 KiB

/// Default LRU read cache capacity (number of pages).
constexpr uint32_t kDefaultReadCachePages = 128;

} // namespace xstd
