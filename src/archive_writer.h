#pragma once

// ---------------------------------------------------------------------------
// archive_writer.h — ArchiveWriter: creates and populates a .xstd archive.
//
// Usage:
//   ArchiveWriterOptions opts;
//   opts.page_size    = PageSize::PAGE_64K;
//   opts.codec        = CompressionCodec(CompressionType::ZSTD, CompressionLevel::XSTD_greedy);
//   opts.encryption   = EncryptionAlgorithm::AES_GCM_V1;
//   opts.key          = my_32_byte_key;
//
//   ArchiveWriter writer("archive.xstd", opts);
//   writer.AddFile("docs/readme.txt", file_bytes);
//   writer.AddFile("data/input.csv",  csv_bytes);
//   writer.Finalise();   // writes catalog + footer, closes file
//
// After Finalise() the writer is closed and cannot be used again.
// ---------------------------------------------------------------------------

#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "compression.h"
#include "encryption.h"
#include "metadata.h"
#include "catalog.h"
#include "page_manager.h"

namespace xstd {

// ---------------------------------------------------------------------------
// ArchiveWriterOptions
// ---------------------------------------------------------------------------
struct ArchiveWriterOptions {
    PageSize          page_size  {PageSize::PAGE_64K};
    CompressionCodec  codec      {CompressionType::ZSTD, CompressionLevel::XSTD_greedy};

    /// Encryption descriptor: algorithm + key size packed in one byte.
    /// Use ArchiveEncryption::Make(alg, key_size) or leave default for no encryption.
    ArchiveEncryption encryption {};

    /// Encryption key bytes. Required when encryption.IsEncrypted().
    /// Must match the key size encoded in encryption (16, 24, or 32 bytes).
    std::vector<uint8_t> key;
};

// ---------------------------------------------------------------------------
// ArchiveWriter
class ArchiveWriter {
public:
    explicit ArchiveWriter(const std::filesystem::path& path,
                           ArchiveWriterOptions         opts = {});
    ~ArchiveWriter();

    // Non-copyable, non-movable.
    ArchiveWriter(const ArchiveWriter&)            = delete;
    ArchiveWriter& operator=(const ArchiveWriter&) = delete;

    /// Add a file from a memory buffer.
    void AddFile(const std::string&       archive_path,
                 std::span<const uint8_t> data);

    /// Add a file from a std::vector (convenience overload).
    void AddFile(const std::string& archive_path, const std::vector<uint8_t>& data) {
        AddFile(archive_path, std::span<const uint8_t>(data));
    }

    /// Add a file from disk.
    void AddFileFromDisk(const std::filesystem::path& source,
                         const std::string&           archive_path);

    /// Writes catalog + footer, flushes and closes the archive.
    void Finalise();

private:
    ArchiveWriterOptions         opts_;
    std::ofstream                file_;
    std::unique_ptr<ICompressor> compressor_;
    std::unique_ptr<IEncryptor>  encryptor_;
    Catalog                      catalog_;
    PageManager                  page_mgr_;
    int32_t                      next_page_id_{0};
    std::size_t                  file_count_{0};
    bool                         finalised_{false};

    void       ValidateOptions() const;
    void       WriteArchiveHeader();
    PageHeader WritePage(std::span<const uint8_t> chunk, PageType type);
    int32_t    AllocatePageId();
};

} // namespace xstd
