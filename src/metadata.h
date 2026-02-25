#pragma once

// ---------------------------------------------------------------------------
// metadata.h — On-disk and in-memory structures for the Xstd archive format.
//
// Disk layout (byte order: little-endian):
//
//  [ArchiveHeader  32 bytes]
//  [Pages ...]
//    For each page: [PageHeader 32 bytes][page data — compressed_size bytes]
//  [Catalog region — serialised B+ Tree]
//  [ArchiveFooter  32 bytes]
// ---------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "constants.h"
#include "types.h"

namespace xstd {

// ---------------------------------------------------------------------------
// ArchiveHeader — written at byte offset 0, fixed 32 bytes.
//
// On-disk layout:
//   offset  0 : uint32_t magic          (0x58535444 = 'XSTD')
//   offset  4 : int32_t  version
//   offset  8 : uint8_t  page_size      (PageSize enum)
//   offset  9 : uint8_t  encryption_alg (EncryptionAlgorithm enum)
//   offset 10 : uint8_t  aes_key_size   (AesKeySize enum — 16/24/32)
//   offset 11 : uint8_t  flags          (bit 0 = has_encryption, bit 1 = partial_metadata_protection)
//   offset 12 : int64_t  num_pages
//   offset 20 : uint8_t  reserved[12]
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct ArchiveHeader {
    uint32_t magic          {kMagic};
    int32_t  version        {kCurrentVersion};
    uint8_t  page_size      {static_cast<uint8_t>(PageSize::PAGE_64K)};
    uint8_t  encryption_alg {static_cast<uint8_t>(EncryptionAlgorithm::NONE)};
    uint8_t  aes_key_size   {static_cast<uint8_t>(AesKeySize::AES_256)};
    uint8_t  flags          {0};
    int64_t  num_pages      {0};
    uint8_t  reserved[12]   {};

    static_assert(sizeof(uint32_t) + sizeof(int32_t) +
                  3 * sizeof(uint8_t) + sizeof(uint8_t) +
                  sizeof(int64_t) + 12 == kArchiveHeaderSize,
                  "ArchiveHeader must be exactly 32 bytes");

    [[nodiscard]] bool IsEncrypted() const noexcept { return (flags & 0x01) != 0; }
    [[nodiscard]] bool HasPartialMetadataProtection() const noexcept { return (flags & 0x02) != 0; }

    void SetEncrypted(bool v) noexcept {
        flags = v ? (flags | 0x01) : (flags & ~0x01);
    }
    void SetPartialMetadataProtection(bool v) noexcept {
        flags = v ? (flags | 0x02) : (flags & ~0x02);
    }
};
#pragma pack(pop)
static_assert(sizeof(ArchiveHeader) == kArchiveHeaderSize, "ArchiveHeader size mismatch");

// ---------------------------------------------------------------------------
// PageHeader — written immediately before each page's data, fixed 32 bytes.
//
// On-disk layout:
//   offset  0 : int32_t  page_id
//   offset  4 : int32_t  offset_from_archive_start  (high-level; actual seek = this value)
//   offset  8 : uint8_t  page_type         (PageType enum)
//   offset  9 : uint8_t  encoding          (Encoding enum)
//   offset 10 : uint8_t  compression_codec (CompressionCodec::raw)
//   offset 11 : uint8_t  flags             (bit 0 = encrypted)
//   offset 12 : int32_t  uncompressed_size
//   offset 16 : int32_t  compressed_size
//   offset 20 : uint32_t crc32             (CRC32 of compressed plaintext data)
//   offset 24 : uint8_t  iv[8]             (first 8 bytes of IV/nonce; remaining bytes follow data or are zero)
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct PageHeader {
    int32_t  page_id            {0};
    int32_t  offset             {0};   // byte offset from start of archive file
    uint8_t  page_type          {static_cast<uint8_t>(PageType::DATA_PAGE)};
    uint8_t  encoding           {static_cast<uint8_t>(Encoding::PLAIN)};
    uint8_t  compression_codec  {0};   // CompressionCodec::raw
    uint8_t  flags              {0};   // bit 0 = page is encrypted
    int32_t  uncompressed_size  {0};
    int32_t  compressed_size    {0};
    uint32_t crc32              {0};
    uint8_t  iv[8]              {};    // partial IV storage; full IV is prepended to ciphertext for AES-GCM/CTR

    [[nodiscard]] bool IsEncrypted() const noexcept { return (flags & 0x01) != 0; }
    void SetEncrypted(bool v) noexcept {
        flags = v ? (flags | 0x01) : (flags & ~0x01);
    }
    [[nodiscard]] CompressionCodec Codec() const noexcept {
        CompressionCodec c; c.raw = compression_codec; return c;
    }
    [[nodiscard]] PageType    Type()     const noexcept { return static_cast<PageType>(page_type); }
    [[nodiscard]] Encoding    GetEncoding() const noexcept { return static_cast<Encoding>(encoding); }
};
#pragma pack(pop)
static_assert(sizeof(PageHeader) == kPageHeaderSize, "PageHeader size mismatch");

// ---------------------------------------------------------------------------
// FileMetadata — in-memory representation of one archived file.
// Serialised into the Catalog (B+ Tree value type), NOT written raw to disk.
// ---------------------------------------------------------------------------
struct FileMetadata {
    std::string file_name;                          // UTF-8 path as stored in archive (e.g. "docs/readme.txt")
    std::vector<PageHeader> pages;                  // ordered list of page headers for this file
    int64_t  signature        {0};                  // user-settable signature field
    std::array<uint8_t, kSha256Size> checksum {};   // SHA-256 of original uncompressed file content
    int64_t  created_time     {0};                  // Unix timestamp (seconds since epoch)
    int64_t  last_modified_time {0};                // Unix timestamp
    int64_t  original_size    {0};                  // total uncompressed size in bytes
};

// ---------------------------------------------------------------------------
// ArchiveFooter — written at the very end of the file, fixed 32 bytes.
//
// On-disk layout:
//   offset  0 : int64_t  catalog_offset  (byte offset from start of archive)
//   offset  8 : int64_t  catalog_size    (byte length of the catalog region)
//   offset 16 : int64_t  num_files       (number of files in catalog)
//   offset 24 : uint32_t footer_magic    (0x58454E44 = 'XEND')
//   offset 28 : uint32_t reserved
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct ArchiveFooter {
    int64_t  catalog_offset {0};
    int64_t  catalog_size   {0};
    int64_t  num_files      {0};
    uint32_t footer_magic   {kFooterMagic};
    uint32_t reserved       {0};
};
#pragma pack(pop)
static_assert(sizeof(ArchiveFooter) == kArchiveFooterSize, "ArchiveFooter size mismatch");

} // namespace xstd
