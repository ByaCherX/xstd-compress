#pragma once

// ---------------------------------------------------------------------------
// archive_reader.h — ArchiveReader: reads files from a .xstd archive.
//
// Usage:
//   ArchiveReaderOptions opts;
//   opts.key = my_32_byte_key;   // if archive is encrypted
//
//   ArchiveReader reader("archive.xstd", opts);
//   if (auto err = reader.Open(); err != (kSuccess)) { /* handle */ }
//
//   auto files = reader.ListFiles();          // all file paths
//   std::vector<uint8_t> data;
//   reader.ExtractFile("docs/readme.txt", data);
//   reader.ExtractFileToDisk("data/input.csv", "/tmp/input.csv");
//
// LRU page cache reduces I/O for repeated reads of the same pages.
// IEncryptor are created once in Open() and reused across
// ---------------------------------------------------------------------------

#include <filesystem>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "xstd_errors.h"
#include "compression.h"
#include "encryption.h"
#include "constants.h"
#include "metadata.h"
#include "catalog.h"
#include "iohandler.h"

namespace xstd {

// ---------------------------------------------------------------------------
// ArchiveReaderOptions
// ---------------------------------------------------------------------------
struct ArchiveReaderOptions {
    /// Decryption key (ignored if archive is not encrypted).
    std::vector<uint8_t> key;

    /// LRU cache: max number of decompressed pages to keep in memory.
    uint32_t cache_capacity{kDefaultReadCachePages};
};

// ---------------------------------------------------------------------------
// ArchiveReader
// ---------------------------------------------------------------------------
class ArchiveReader {
public:
    explicit ArchiveReader(const std::filesystem::path& path,
                           ArchiveReaderOptions         opts = {});

    /// Construct with a shared IOHandler (ReadWrite mode).
    /// The reader does NOT own the IOHandler; the caller must keep it alive.
    explicit ArchiveReader(std::shared_ptr<IOHandler>   io,
                           ArchiveReaderOptions         opts = {});

    // Non-copyable.
    ArchiveReader(const ArchiveReader&)            = delete;
    ArchiveReader& operator=(const ArchiveReader&) = delete;

    [[nodiscard]] XSTD_Result Open();
    XSTD_Result Close();

    /// Re-read the catalog from disk (after in-place mutations).
    /// Clears the LRU cache.
    [[nodiscard]] XSTD_Result ReloadCatalog();

    // -- List --
    [[nodiscard]] std::vector<std::string>    ListFiles() const;
    [[nodiscard]] std::vector<std::string>    ListDeletedFiles() const;
    [[nodiscard]] std::vector<std::string>    ListDirectory(const std::string& prefix) const;
    [[nodiscard]] std::optional<FileMetadata> Stat(const std::string& filename) const;

    // -- Extract --
    [[nodiscard]] XSTD_Result ExtractFile(const std::string& filename,
                                       std::vector<uint8_t>& out);
    [[nodiscard]] XSTD_Result ExtractFileToDisk(const std::string& filename,
                                      const std::filesystem::path& dest);

    // -- Soft-delete recovery --
    /// Returns the data of a logically-deleted file, or std::nullopt if not found / unrecoverable.
    [[nodiscard]] std::optional<std::vector<uint8_t>> RecoverFile(const std::string& filename);

    // -- Archive info --
    [[nodiscard]] const ArchiveHeader& Header()    const noexcept { return header_; }
    [[nodiscard]] std::size_t          FileCount() const noexcept { return catalog_.FileCount(); }

    /// Direct access to the internal catalog (for ReadWrite shared mode).
    [[nodiscard]] const Catalog& GetCatalog() const noexcept { return catalog_; }

private:
    IOHandler& IO() { return shared_io_ ? *shared_io_ : *io_; }
    const IOHandler& IO() const { return shared_io_ ? *shared_io_ : *io_; }

    std::filesystem::path            path_;
    ArchiveReaderOptions             opts_;
    std::shared_ptr<IOHandler>       shared_io_;   ///< Shared I/O (ReadWrite mode)
    std::optional<IOHandler>         io_;          ///< Owned I/O (ReadOnly mode, set in Open())
    ArchiveHeader                    header_{};
    Catalog                          catalog_;
    std::unique_ptr<ICompressor>     compressor_;
    std::unique_ptr<IEncryptor>      encryptor_;   ///< Created once in Open(), nullptr if not encrypted

    // -- LRU page cache --
    using CacheKey   = int32_t;
    using CacheValue = std::vector<uint8_t>;
    struct LruEntry { CacheKey key; CacheValue value; };

    std::list<LruEntry> lru_order_;
    std::unordered_map<CacheKey, std::list<LruEntry>::iterator> lru_cache_;

    void ReadAndValidateHeader();
    void ReadAndValidateCatalog();

    /// Read a single page from disk, decrypt and decompress as needed, and return the data.
    [[nodiscard]] const std::vector<uint8_t>& ReadPage(const PageHeader& ph);

    [[nodiscard]] std::vector<uint8_t> AssembleFile(const FileMetadata& meta);
};

} // namespace xstd
