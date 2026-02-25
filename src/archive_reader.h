#pragma once

// ---------------------------------------------------------------------------
// archive_reader.h — ArchiveReader: reads files from a .xstd archive.
//
// Usage:
//   ArchiveReaderOptions opts;
//   opts.key = my_32_byte_key;   // if archive is encrypted
//
//   ArchiveReader reader("archive.xstd", opts);
//   reader.Open();
//
//   auto files = reader.ListFiles();          // all file paths
//   auto data  = reader.ExtractFile("docs/readme.txt");
//   reader.ExtractFileToDisk("data/input.csv", "/tmp/input.csv");
//
// LRU page cache reduces I/O for repeated reads of the same pages.
// ---------------------------------------------------------------------------

#include <filesystem>
#include <fstream>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "compression.h"
#include "encryption.h"
#include "constants.h"
#include "metadata.h"
#include "catalog.h"

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

    // Non-copyable.
    ArchiveReader(const ArchiveReader&)            = delete;
    ArchiveReader& operator=(const ArchiveReader&) = delete;

    void Open();
    void Close() { file_.close(); lru_cache_.clear(); lru_order_.clear(); }

    // -- List --
    [[nodiscard]] std::vector<std::string>      ListFiles()                         const;
    [[nodiscard]] std::vector<std::string>      ListDirectory(const std::string& prefix) const;
    [[nodiscard]] std::optional<FileMetadata>   Stat(const std::string& archive_path) const;

    // -- Extract --
    [[nodiscard]] std::vector<uint8_t> ExtractFile(const std::string& archive_path);
    void ExtractFileToDisk(const std::string&           archive_path,
                           const std::filesystem::path& dest);

    // -- Archive info --
    [[nodiscard]] const ArchiveHeader& Header()    const noexcept { return header_; }
    [[nodiscard]] std::size_t          FileCount() const noexcept { return catalog_.FileCount(); }

private:
    std::filesystem::path path_;
    ArchiveReaderOptions  opts_;
    std::ifstream         file_;
    ArchiveHeader         header_{};
    Catalog               catalog_;

    // -- LRU page cache --
    using CacheKey   = int32_t;
    using CacheValue = std::vector<uint8_t>;
    struct LruEntry { CacheKey key; CacheValue value; };

    std::list<LruEntry>                              lru_order_;
    std::unordered_map<CacheKey,
        std::list<LruEntry>::iterator>               lru_cache_;

    void ReadAndValidateHeader();
    void ReadAndValidateCatalog();
    [[nodiscard]] const std::vector<uint8_t>& ReadPage(const PageHeader& ph);
    [[nodiscard]] std::vector<uint8_t>        AssembleFile(const FileMetadata& meta);
};

} // namespace xstd
