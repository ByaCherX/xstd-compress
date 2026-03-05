#include "archive.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace xstd {

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

Archive::Archive(const std::filesystem::path& path, ArchiveOptions opts)
    : path_(path), opts_(std::move(opts))
{}

Archive::~Archive() {
    (void)Close();
}

// ---------------------------------------------------------------------------
// Open
// ---------------------------------------------------------------------------

XSTD_Result Archive::Open() {
    if (opts_.read_write) {
        // ---- ReadWrite mode: single shared IOHandler ----
        try {
            shared_io_ = std::make_shared<IOHandler>(path_,
                                                    IOHandler::OpenMode::ReadWrite);
        } catch (const XstdError& e) {
            return XSTD_returnError(e.code());
        } catch (...) {
            return XSTD_returnError(kCannotOpenFile);
        }

        ArchiveReaderOptions ropts;
        ropts.key            = opts_.key;
        ropts.cache_capacity = opts_.cache_capacity;

        reader_ = std::make_unique<ArchiveReader>(shared_io_, std::move(ropts));
        if (XSTD_Result r = reader_->Open(); XSTD_isError(r)) {
            reader_.reset();
            shared_io_.reset();
            return r;
        }

        // Compute catalog_offset from the footer.
        // Footer is at the end; catalog_offset is the first byte after page data.
        // We need to find it from the reader's internal state via the footer.
        // We can derive it: catalog_offset = file_size - footer_stored_size - catalog_size.
        // Simpler: just track the catalog write position via the header information.
        // For now: the safest approach is scanning the footer.
        {
            const auto& hdr = reader_->Header();
            std::size_t footer_stored_size = sizeof(ArchiveFooter);
            if (hdr.encryption.IsEncrypted()) {
                auto enc = EncryptorFactory::Create(hdr.encryption.GetAlgorithm());
                if (enc)
                    footer_stored_size = enc->IvSize() + sizeof(ArchiveFooter)
                                         + enc->TagSize();
            }
            // Read the footer to get catalog_offset.
            const int64_t footer_off =
                shared_io_->FileSize() - static_cast<int64_t>(footer_stored_size);
            auto footer_span = shared_io_->ReadAt(footer_off, footer_stored_size);

            ArchiveFooter footer{};
            if (hdr.encryption.IsEncrypted()) {
                auto enc = EncryptorFactory::Create(hdr.encryption.GetAlgorithm());
                auto plain = enc->Decrypt({footer_span.data(), footer_span.size()},
                                          opts_.key);
                std::memcpy(&footer, plain.data(), sizeof(ArchiveFooter));
            } else {
                std::memcpy(&footer, footer_span.data(), sizeof(ArchiveFooter));
            }
            catalog_offset_ = footer.catalog_offset;
        }

        // Compute next_page_id: scan all files to find the max page_id.
        int32_t max_page_id = -1;
        for (const auto& f : reader_->ListFiles()) {
            auto meta = reader_->Stat(f);
            if (!meta) continue;
            for (const auto& ph : meta->pages)
                if (ph.page_id > max_page_id) max_page_id = ph.page_id;
        }
        for (const auto& f : reader_->ListDeletedFiles()) {
            auto meta = reader_->Stat(f);
            if (!meta) continue;
            for (const auto& ph : meta->pages)
                if (ph.page_id > max_page_id) max_page_id = ph.page_id;
        }

        ArchiveWriterOptions wopts;
        wopts.codec = opts_.codec;
        wopts.key   = opts_.key;

        writer_ = std::make_unique<ArchiveWriter>(path_, std::move(wopts));
        if (XSTD_Result r = writer_->InitAppend(
                shared_io_, reader_->Header(),
                reader_->GetCatalog(),
                max_page_id + 1,
                reader_->FileCount());
            XSTD_isError(r))
        {
            writer_.reset();
            reader_->Close();
            reader_.reset();
            shared_io_.reset();
            return r;
        }

        opened_ = true;
        return XSTD_returnSuccess();
    }

    // ---- Normal (ReadOnly) mode ----
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
        if (!IsReadWrite()) {
            if (XSTD_Result r = writer_->Finalise(); XSTD_isError(r)) result = r;
        }
        writer_.reset();
    }
    if (reader_) {
        if (XSTD_Result r = reader_->Close(); XSTD_isError(r)) result = r;
        reader_.reset();
    }
    shared_io_.reset();

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

    if (!opened_ || !reader_)
        return XSTD_returnError(kInvalidArgument);

    // ReadWrite fast path: append pages in-place, then commit catalog.
    if (IsReadWrite()) {
        shared_io_->SetAppendPosition(catalog_offset_);
        if (auto r = writer_->AddFile(dest_path, data); XSTD_isError(r))
            return r;
        return CommitCatalog();
    }

    // Fallback: full rewrite.
    std::vector<uint8_t> data_copy(data.begin(), data.end());
    std::string          path_copy = dest_path;

    return Rewrite([&](ArchiveWriter& w, ArchiveReader& r) -> XSTD_Result {
        for (const auto& f : r.ListFiles()) {
            std::vector<uint8_t> buf;
            if (auto e = r.ExtractFile(f, buf); XSTD_isError(e)) return e;
            if (auto e = w.AddFile(f, buf);      XSTD_isError(e)) return e;
        }
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

    if (IsReadWrite()) {
        shared_io_->SetAppendPosition(catalog_offset_);
        if (auto r = writer_->AddFileFromDisk(source, dest_path); XSTD_isError(r))
            return r;
        return CommitCatalog();
    }

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

XSTD_Result Archive::DeleteFile(const std::string& path, bool soft_delete) {
    if (!opened_ || !reader_)
        return XSTD_returnError(kInvalidArgument);

    if (!reader_->Stat(path).has_value())
        return XSTD_returnError(kFileNotFound);

    if (IsReadWrite()) {
        // In-place: patch page header flags + rewrite catalog.
        if (auto r = writer_->DeleteFile(path, soft_delete); XSTD_isError(r))
            return r;
        shared_io_->SetAppendPosition(catalog_offset_);
        return CommitCatalog();
    }

    std::string path_copy   = path;
    bool        soft_copy   = soft_delete;

    return Rewrite([&](ArchiveWriter& w, ArchiveReader& r) -> XSTD_Result {
        for (const auto& f : r.ListFiles()) {
            if (!soft_copy && f == path_copy) continue;  // hard delete: omit from rewrite
            std::vector<uint8_t> buf;
            if (auto e = r.ExtractFile(f, buf); XSTD_isError(e)) return e;
            if (auto e = w.AddFile(f, buf);      XSTD_isError(e)) return e;
        }
        if (soft_copy)
            return w.DeleteFile(path_copy, true);
        return XSTD_returnSuccess();
    });
}

// ---------------------------------------------------------------------------
// RenameFile
// ---------------------------------------------------------------------------

XSTD_Result Archive::RenameFile(const std::string& old_path,
                                const std::string& new_path)
{
    if (!opened_ || !reader_)
        return XSTD_returnError(kInvalidArgument);

    if (!reader_->Stat(old_path).has_value())
        return XSTD_returnError(kFileNotFound);

    if (IsReadWrite()) {
        // Catalog-only: move entry from old key to new key.
        auto meta = reader_->Stat(old_path);
        if (!meta) return XSTD_returnError(kFileNotFound);
        auto& cat = writer_->GetCatalog();
        cat.Erase(old_path);
        FileMetadata m = *meta;
        m.file_name = new_path;
        cat.Insert(new_path, m);
        shared_io_->SetAppendPosition(catalog_offset_);
        return CommitCatalog();
    }

    std::string old_copy = old_path;
    std::string new_copy = new_path;

    return Rewrite([&](ArchiveWriter& w, ArchiveReader& r) -> XSTD_Result {
        for (const auto& f : r.ListFiles()) {
            std::vector<uint8_t> buf;
            if (auto e = r.ExtractFile(f, buf); XSTD_isError(e)) return e;
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
// RecoverFile
// ---------------------------------------------------------------------------

std::optional<std::vector<uint8_t>> Archive::RecoverFile(const std::string& path) {
    if (!reader_) return std::nullopt;
    return reader_->RecoverFile(path);
}

// ---------------------------------------------------------------------------
// Private — CommitCatalog (ReadWrite mode)
// ---------------------------------------------------------------------------

XSTD_Result Archive::CommitCatalog() {
    // Remember where the catalog starts (for the next mutation).
    const int64_t new_catalog_offset = shared_io_->AppendPosition();

    if (auto r = writer_->WriteCatalogAndFooter(); XSTD_isError(r))
        return r;

    // Truncate any leftover bytes beyond the new end-of-file.
    if (auto r = shared_io_->Truncate(shared_io_->AppendPosition()); XSTD_isError(r))
        return r;

    catalog_offset_ = new_catalog_offset;

    // Refresh the reader's view of the catalog.
    if (auto r = reader_->ReloadCatalog(); XSTD_isError(r))
        return r;

    return XSTD_returnSuccess();
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
        : CompressionCodec{CompressionType::UNCOMPRESSED, CompressionLevel::XSTD_RESERVED_LEVEL};

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
