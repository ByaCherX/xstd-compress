#pragma once

// ---------------------------------------------------------------------------
// archive.h — Archive: high-level API combining ArchiveReader and ArchiveWriter.
//
// Archive provides a single unified interface for all common operations on
// .xstd archives.  Internally it owns an ArchiveReader (for query/extract
// operations) and an ArchiveWriter (for mutation operations), coordinating
// them automatically.
//
// Lower-level direct access is still available through Reader() / Writer()
// accessors when fine-grained control is needed.
//
// Usage (open existing archive):
//   ArchiveOptions opts;
//   opts.key = my_key;
//   Archive arch("my.xstd", opts);
//   arch.Open();
//
//   std::vector<uint8_t> data;
//   arch.ExtractFile("docs/readme.txt", data);
//   arch.AddFile("new/file.txt", file_bytes);   // rewrites archive internally
//   arch.Close();
//
// Usage (create new archive):
//   ArchiveOptions opts;
//   opts.codec = CompressionCodec{CompressionType::ZSTD, CompressionLevel::XSTD_greedy};
//   Archive arch("new.xstd", opts);
//   arch.Create();
//   arch.AddFile("readme.txt", data);
//   arch.Close();
//
// Thread safety:
//   An Archive instance must not be accessed concurrently from multiple threads
//   without external synchronisation.  SetThreadCount() controls the number of
//   threads used internally for parallel page I/O.
// ---------------------------------------------------------------------------

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "xstd_errors.h"
#include "metadata.h"
#include "archive_reader.h"
#include "archive_writer.h"

namespace xstd {

// ---------------------------------------------------------------------------
// ArchiveOptions — unified options for both open and create modes
// ---------------------------------------------------------------------------
struct ArchiveOptions {
    // --- Read / decrypt ---
    std::vector<uint8_t> key;               ///< Encryption key (read & write)
    uint32_t cache_capacity{kDefaultReadCachePages}; ///< LRU page cache size

    // --- Write / compress ---
    PageSize          page_size  {PageSize::PAGE_64K};
    CompressionCodec  codec      {CompressionType::ZSTD, CompressionLevel::XSTD_greedy};
    ArchiveEncryption encryption {};         ///< Default = no encryption

    // --- Parallelism (Phase 4 hook) ---
    uint32_t thread_count{1};               ///< 0 = hardware_concurrency (Phase 4)
};

// ---------------------------------------------------------------------------
// Archive
// ---------------------------------------------------------------------------
class Archive {
public:
    explicit Archive(const std::filesystem::path& path,
                     ArchiveOptions               opts = {});
    ~Archive();

    // Non-copyable.
    Archive(const Archive&)            = delete;
    Archive& operator=(const Archive&) = delete;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Open an existing archive for reading (and optional mutation).
    [[nodiscard]] XSTD_Result Open();

    /// Create a new (empty) archive for writing.
    [[nodiscard]] XSTD_Result Create();

    /// Flush and close the archive.
    [[nodiscard]] XSTD_Result Close();

    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------

    /// Set the number of worker threads for parallel page I/O.
    /// 0 = std::thread::hardware_concurrency().  Currently reserved for Phase 4.
    void SetThreadCount(uint32_t n);

    // -----------------------------------------------------------------------
    // Mutation (requires Open or Create)
    // -----------------------------------------------------------------------

    /// Add a file from memory.
    [[nodiscard]] XSTD_Result AddFile(const std::string& dest_path,
                                std::span<const uint8_t> data);
    [[nodiscard]] XSTD_Result AddFile(const std::string& dest_path,
                             const std::vector<uint8_t>& data) {
        return AddFile(dest_path, std::span<const uint8_t>(data));
    }

    /// Add a file from disk.  Overload of AddFile (replaces AddFileFromDisk).
    [[nodiscard]] XSTD_Result AddFile(const std::string& dest_path,
                            const std::filesystem::path& source);

    /// Logically delete a file (soft-delete; data remains recoverable).
    [[nodiscard]] XSTD_Result DeleteFile(const std::string& path);

    /// Rename a file (updates catalog key; no page data rewrite needed).
    [[nodiscard]] XSTD_Result RenameFile(const std::string& old_path,
                                         const std::string& new_path);

    // -----------------------------------------------------------------------
    // Extract (requires Open)
    // -----------------------------------------------------------------------

    /// Extract to memory.
    [[nodiscard]] XSTD_Result ExtractFile(const std::string& path,
                                       std::vector<uint8_t>& out);

    /// Extract to a file on disk (same name: ExtractFile overload).
    [[nodiscard]] XSTD_Result ExtractFile(const std::string& path,
                                const std::filesystem::path& dest);

    // -----------------------------------------------------------------------
    // Catalog queries (requires Open)
    // -----------------------------------------------------------------------

    [[nodiscard]] std::vector<std::string>    ListFiles() const;
    [[nodiscard]] std::vector<std::string>    ListDeletedFiles() const;
    [[nodiscard]] std::vector<std::string>    ListDirectory(const std::string& prefix) const;
    [[nodiscard]] std::optional<FileMetadata> Stat(const std::string& path) const;
    [[nodiscard]] const ArchiveHeader&        Header() const;
    [[nodiscard]] std::size_t                 FileCount() const;

    // -----------------------------------------------------------------------
    // Low-level access
    // -----------------------------------------------------------------------

    /// Direct access to the underlying reader (nullptr if not opened).
    [[nodiscard]] ArchiveReader* Reader() const noexcept { return reader_.get(); }

    /// Direct access to the underlying writer (nullptr if not in create/rewrite).
    [[nodiscard]] ArchiveWriter* Writer() const noexcept { return writer_.get(); }

private:
    std::filesystem::path path_;
    ArchiveOptions        opts_;
    uint32_t              thread_count_;

    std::unique_ptr<ArchiveReader> reader_;
    std::unique_ptr<ArchiveWriter> writer_;

    bool opened_{false};   ///< True when Open() succeeded
    bool created_{false};  ///< True when Create() succeeded

    // Internal: rewrite the entire archive through a user-supplied transform.
    // The transform lambda receives (new_writer, existing_reader) and may add/skip
    // files as needed.  On success the temp file atomically replaces the original.
    [[nodiscard]] XSTD_Result Rewrite(
        const std::function<XSTD_Result(ArchiveWriter&, ArchiveReader&)>& transform);
};

} // namespace xstd
