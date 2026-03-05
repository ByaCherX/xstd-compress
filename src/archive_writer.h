#pragma once

// ---------------------------------------------------------------------------
// archive_writer.h — ArchiveWriter: creates and populates a .xstd archive.
//
// Usage:
//   ArchiveWriterOptions opts;
//   opts.page_size    = PageSize::PAGE_64K;
//   opts.codec        = CompressionCodec(CompressionType::ZSTD, CompressionLevel::XSTD_greedy);
//   opts.encryption   = ArchiveEncryption::Make(EncryptionAlgorithm::AES_GCM_V1, AesKeySize::AES_256);
//   opts.key          = my_32_byte_key;
//
//   ArchiveWriter writer("archive.xstd", opts);
//   if (auto err = writer.Init(); err != (kSuccess)) { /* handle */ }
//   writer.AddFile("docs/readme.txt", file_bytes);
//   writer.AddFile("data/input.csv",  csv_bytes);
//   writer.Finalise();   // writes catalog + footer, closes file
//
// After Finalise() the writer is closed and cannot be used again.
// ---------------------------------------------------------------------------

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "xstd_errors.h"
#include "compression.h"
#include "encryption.h"
#include "metadata.h"
#include "catalog.h"
#include "page_manager.h"
#include "iohandler.h"

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

    /// Opens the archive file and writes the archive header.
    /// Must be called before any AddFile / DeleteFile / Finalise calls.
    [[nodiscard]] XSTD_Result Init();

    /// Add a file from a memory buffer.
    [[nodiscard]] XSTD_Result AddFile(const std::string& filename,
                                std::span<const uint8_t> data);

    /// Add a file from a std::vector (convenience overload).
    [[nodiscard]] XSTD_Result AddFile(const std::string& filename,
                             const std::vector<uint8_t>& data) {
        return AddFile(filename, std::span<const uint8_t>(data));
    }

    /// Add a file from disk.
    [[nodiscard]] XSTD_Result AddFileFromDisk(const std::filesystem::path& source,
                                              const std::string&           filename);

    /// Logically delete a previously-added file.
    /// Marks the file's catalog entry and each of its page headers on disk as deleted.
    /// Returns kFileNotFound if the file was not found.
    /// Returns kSuccess if the file was (or was already) deleted.
    /// Physical data is preserved; use ArchiveReader::RecoverFile() to restore.
    [[nodiscard]] XSTD_Result DeleteFile(const std::string& filename);

    /// Writes catalog + footer, flushes and closes the archive.
    [[nodiscard]] XSTD_Result Finalise();

private:
    std::filesystem::path        path_;
    ArchiveWriterOptions         opts_;
    std::optional<IOHandler>     io_;          ///< Positional-write I/O (set in Init())
    std::unique_ptr<ICompressor> compressor_;
    std::unique_ptr<IEncryptor>  encryptor_;
    Catalog                      catalog_;
    PageManager                  page_mgr_;
    int32_t                      next_page_id_{0};
    std::size_t                  file_count_{0};
    bool                         finalised_{false};

    void ValidateOptions() const;
    void WriteArchiveHeader();

    /// Write a single page with explicit type, encoding and per-page codec.
    /// `per_page_codec` defaults to opts_.codec; passing a different value
    /// allows individual pages to use a different compression algorithm.
    PageHeader WritePage(std::span<const uint8_t> chunk,
                         PageType                 type = PageType::DATA_PAGE,
                         Encoding                 encoding = Encoding::PLAIN,
                         CompressionCodec         per_page_codec = {});

    int32_t AllocatePageId();
};

} // namespace xstd
