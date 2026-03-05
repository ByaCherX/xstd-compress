#include "archive.h"

#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace xstd {

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

Archive::Archive(const std::filesystem::path& path, ArchiveOptions opts)
    : path_(path), opts_(std::move(opts)),
      thread_count_(opts_.thread_count == 0
          ? static_cast<uint32_t>(std::thread::hardware_concurrency())
          : opts_.thread_count)
{}

Archive::~Archive() {
    (void)Close();
}

void Archive::SetThreadCount(uint32_t n) {
    thread_count_ = (n == 0)
        ? static_cast<uint32_t>(std::thread::hardware_concurrency())
        : n;
    // TODO (Phase 4): propagate to internal thread pool when implemented.
}

// ---------------------------------------------------------------------------
// Open
// ---------------------------------------------------------------------------

XSTD_Result Archive::Open() {
    ArchiveReaderOptions ropts;
    ropts.key             = opts_.key;
    ropts.cache_capacity  = opts_.cache_capacity;

    reader_ = std::make_unique<ArchiveReader>(path_, std::move(ropts));
    if (XSTD_Result r = reader_->Open(); XSTD_isError(r)) {
        reader_.reset();
        return r;
    }

    opened_ = true;
    return XSTD_returnSuccess();
}

// ---------------------------------------------------------------------------
// Create
// ---------------------------------------------------------------------------

XSTD_Result Archive::Create() {
    ArchiveWriterOptions wopts;
    wopts.page_size  = opts_.page_size;
    wopts.codec      = opts_.codec;
    wopts.encryption = opts_.encryption;
    wopts.key        = opts_.key;

    writer_ = std::make_unique<ArchiveWriter>(path_, std::move(wopts));
    if (XSTD_Result r = writer_->Init(); XSTD_isError(r)) {
        writer_.reset();
        return r;
    }

    created_ = true;
    return XSTD_returnSuccess();
}

// ---------------------------------------------------------------------------
// Close
// ---------------------------------------------------------------------------

XSTD_Result Archive::Close() {
    XSTD_Result result = XSTD_returnSuccess();

    if (writer_) {
        if (XSTD_Result r = writer_->Finalise(); XSTD_isError(r)) result = r;
        writer_.reset();
    }
    if (reader_) {
        if (XSTD_Result r = reader_->Close(); XSTD_isError(r)) result = r;
        reader_.reset();
    }

    opened_  = false;
    created_ = false;
    return result;
}

// ---------------------------------------------------------------------------
// AddFile (from memory)
// ---------------------------------------------------------------------------

XSTD_Result Archive::AddFile(const std::string&       dest_path,
                             std::span<const uint8_t> data)
{
    // Fast path: we are in create mode, write directly.
    if (created_ && writer_)
        return writer_->AddFile(dest_path, data);

    // Open mode: need to rewrite to incorporate new data.
    if (!opened_ || !reader_)
        return XSTD_returnError(kInvalidArgument);

    // Capture data into a local copy so the lambda can own it.
    std::vector<uint8_t> data_copy(data.begin(), data.end());
    std::string          path_copy = dest_path;

    return Rewrite([&](ArchiveWriter& w, ArchiveReader& r) -> XSTD_Result {
        // Re-add all currently active files.
        for (const auto& f : r.ListFiles()) {
            std::vector<uint8_t> buf;
            if (auto e = r.ExtractFile(f, buf); XSTD_isError(e)) return e;
            if (auto e = w.AddFile(f, buf);      XSTD_isError(e)) return e;
        }
        // Add the new file.
        return w.AddFile(path_copy, data_copy);
    });
}

// ---------------------------------------------------------------------------
// AddFile (from disk)
// ---------------------------------------------------------------------------

XSTD_Result Archive::AddFile(const std::string&           dest_path,
                             const std::filesystem::path& source)
{
    if (created_ && writer_)
        return writer_->AddFileFromDisk(source, dest_path);

    if (!opened_ || !reader_)
        return XSTD_returnError(kInvalidArgument);

    std::string path_copy   = dest_path;
    auto        source_copy = source;

    return Rewrite([&](ArchiveWriter& w, ArchiveReader& r) -> XSTD_Result {
        for (const auto& f : r.ListFiles()) {
            std::vector<uint8_t> buf;
            if (auto e = r.ExtractFile(f, buf); XSTD_isError(e)) return e;
            if (auto e = w.AddFile(f, buf);      XSTD_isError(e)) return e;
        }
        return w.AddFileFromDisk(source_copy, path_copy);
    });
}

// ---------------------------------------------------------------------------
// DeleteFile
// ---------------------------------------------------------------------------

XSTD_Result Archive::DeleteFile(const std::string& path) {
    if (!opened_ || !reader_)
        return XSTD_returnError(kInvalidArgument);

    // Verify existence before rewriting.
    if (!reader_->Stat(path).has_value())
        return XSTD_returnError(kFileNotFound);

    std::string path_copy = path;

    return Rewrite([&](ArchiveWriter& w, ArchiveReader& r) -> XSTD_Result {
        for (const auto& f : r.ListFiles()) {
            std::vector<uint8_t> buf;
            if (auto e = r.ExtractFile(f, buf); XSTD_isError(e)) return e;
            if (auto e = w.AddFile(f, buf);      XSTD_isError(e)) return e;
        }
        return w.DeleteFile(path_copy);
    });
}

// ---------------------------------------------------------------------------
// RenameFile
// ---------------------------------------------------------------------------

XSTD_Result Archive::RenameFile(const std::string& old_path,
                                const std::string& new_path)
{
    // RenameFile is a catalog-only operation: we insert under the new key
    // and erase the old key — no page data needs to be rewritten.
    // Therefore we rewrite the archive but emit the target file under new_path.
    if (!opened_ || !reader_)
        return XSTD_returnError(kInvalidArgument);

    if (!reader_->Stat(old_path).has_value())
        return XSTD_returnError(kFileNotFound);

    std::string old_copy = old_path;
    std::string new_copy = new_path;

    return Rewrite([&](ArchiveWriter& w, ArchiveReader& r) -> XSTD_Result {
        for (const auto& f : r.ListFiles()) {
            std::vector<uint8_t> buf;
            if (auto e = r.ExtractFile(f, buf); XSTD_isError(e)) return e;
            // Emit under new_path if this is the renamed file.
            const std::string& emit_path = (f == old_copy) ? new_copy : f;
            if (auto e = w.AddFile(emit_path, buf); XSTD_isError(e)) return e;
        }
        return XSTD_returnSuccess();
    });
}

// ---------------------------------------------------------------------------
// ExtractFile (to memory)
// ---------------------------------------------------------------------------

XSTD_Result Archive::ExtractFile(const std::string&    path,
                                 std::vector<uint8_t>& out)
{
    if (!reader_) return XSTD_returnError(kInvalidArgument);
    return reader_->ExtractFile(path, out);
}

// ---------------------------------------------------------------------------
// ExtractFile (to disk — overload)
// ---------------------------------------------------------------------------

XSTD_Result Archive::ExtractFile(const std::string&           path,
                                 const std::filesystem::path& dest)
{
    if (!reader_) return XSTD_returnError(kInvalidArgument);
    return reader_->ExtractFileToDisk(path, dest);
}

// ---------------------------------------------------------------------------
// Catalog queries
// ---------------------------------------------------------------------------

std::vector<std::string> Archive::ListFiles() const {
    if (!reader_) return {};
    return reader_->ListFiles();
}

std::vector<std::string> Archive::ListDeletedFiles() const {
    if (!reader_) return {};
    return reader_->ListDeletedFiles();
}

std::vector<std::string> Archive::ListDirectory(const std::string& prefix) const {
    if (!reader_) return {};
    return reader_->ListDirectory(prefix);
}

std::optional<FileMetadata> Archive::Stat(const std::string& path) const {
    if (!reader_) return std::nullopt;
    return reader_->Stat(path);
}

const ArchiveHeader& Archive::Header() const {
    if (!reader_)
        XSTD_THROW_ERROR_MSG(kInvalidArgument, "Archive::Header() called without an open reader");
    return reader_->Header();
}

std::size_t Archive::FileCount() const {
    if (!reader_) return 0;
    return reader_->FileCount();
}

// ---------------------------------------------------------------------------
// Private — Rewrite
// ---------------------------------------------------------------------------

XSTD_Result Archive::Rewrite(
    const std::function<XSTD_Result(ArchiveWriter&, ArchiveReader&)>& transform)
{
    const std::filesystem::path tmp_path =
        path_.parent_path()
        / (path_.stem().string() + ".xstd_rewrite_tmp" + path_.extension().string());

    // Derive writer options from the existing archive header so that
    // page_size and encryption are preserved on rewrite.
    const ArchiveHeader& hdr = reader_->Header();

    ArchiveWriterOptions wopts;
    wopts.page_size  = static_cast<PageSize>(hdr.page_size);
    wopts.encryption = hdr.encryption;
    wopts.key        = opts_.key;
    wopts.codec      = hdr.IsCompressed()
        ? opts_.codec
        : CompressionCodec{CompressionType::UNCOMPRESSED, CompressionLevel::XSTD_fast};

    ArchiveWriter writer(tmp_path, wopts);
    if (auto r = writer.Init(); XSTD_isError(r)) return r;

    // Run the user-supplied transform.
    XSTD_Result transform_result = transform(writer, *reader_);
    if (XSTD_isError(transform_result)) {
        // Best-effort cleanup of the temp file.
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
        return transform_result;
    }

    if (auto r = writer.Finalise(); XSTD_isError(r)) {
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
        return r;
    }

    // Close current reader before renaming (important on Windows where the
    // file lock must be released before the rename can succeed).
    if (auto r = reader_->Close(); XSTD_isError(r)) return r;
    reader_.reset();

    std::error_code ec;
    std::filesystem::rename(tmp_path, path_, ec);
    if (ec) return XSTD_returnError(kIOError);

    // Re-open the reader on the freshly written archive.
    ArchiveReaderOptions ropts;
    ropts.key            = opts_.key;
    ropts.cache_capacity = opts_.cache_capacity;

    reader_ = std::make_unique<ArchiveReader>(path_, std::move(ropts));
    if (auto r = reader_->Open(); XSTD_isError(r)) {
        reader_.reset();
        opened_ = false;
        return r;
    }

    return XSTD_returnSuccess();
}

} // namespace xstd
