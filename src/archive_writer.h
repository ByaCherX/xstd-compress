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

    /// Initialise for append mode on a shared IOHandler (ReadWrite).
    /// Inherits header settings, catalog, page IDs and file count from an
    /// already-opened archive.  Does NOT write a new archive header.
    [[nodiscard]] XSTD_Result InitAppend(std::shared_ptr<IOHandler> io,
                                         const ArchiveHeader&       hdr,
                                         const Catalog&             catalog,
                                         int32_t                    next_page_id,
                                         std::size_t                file_count);

    /// Add a file from a memory buffer.
    /// @param per_page_codec  Optional. If `raw == 0`, falls back to opts_.codec.
    ///        When provided, every page of this file is written with that codec
    ///        (used to preserve the original codec when rewriting an archive).
    [[nodiscard]] XSTD_Result AddFile(const std::string& filename,
                                std::span<const uint8_t> data,
                                CompressionCodec         per_page_codec = {});

    /// Add a file from a std::vector (convenience overload).
    [[nodiscard]] XSTD_Result AddFile(const std::string& filename,
                             const std::vector<uint8_t>& data,
                             CompressionCodec             per_page_codec = {}) {
        return AddFile(filename, std::span<const uint8_t>(data), per_page_codec);
    }

    /// Add a file from disk.
    /// @param per_page_codec  Optional. See AddFile() for details.
    [[nodiscard]] XSTD_Result AddFileFromDisk(const std::filesystem::path& source,
                                              const std::string&           filename,
                                              CompressionCodec             per_page_codec = {});

    /// Delete a previously-added file.
    /// When soft_delete=true: marks each page header on disk as deleted and keeps the
    ///   catalog entry with deleted=true so the file can be recovered later.
    /// When soft_delete=false (default): zeroes out all page payloads on disk and
    ///   removes the catalog entry entirely (unrecoverable).
    /// Returns kFileNotFound if the file was not found.
    /// Returns kSuccess if the file was (or was already) deleted.
    [[nodiscard]] XSTD_Result DeleteFile(const std::string& filename,
                                         bool soft_delete = false);

    /// Writes catalog + footer, flushes and closes the archive.
    [[nodiscard]] XSTD_Result Finalise();

    /// Write catalog + footer at the current append position, flush.
    /// Does NOT close the IOHandler (used in ReadWrite mode).
    [[nodiscard]] XSTD_Result WriteCatalogAndFooter();

    /// Direct access to the internal catalog.
    [[nodiscard]] Catalog& GetCatalog() noexcept { return catalog_; }
    [[nodiscard]] const Catalog& GetCatalog() const noexcept { return catalog_; }

private:
    IOHandler& IO() { return shared_io_ ? *shared_io_ : *io_; }
    const IOHandler& IO() const { return shared_io_ ? *shared_io_ : *io_; }

    std::filesystem::path        path_;
    ArchiveWriterOptions         opts_;
    std::shared_ptr<IOHandler>   shared_io_;   ///< Shared I/O (ReadWrite mode)
    std::optional<IOHandler>     io_;          ///< Owned I/O (WriteOnly mode, set in Init())
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
