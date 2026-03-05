#include "archive_reader.h"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

#include "sha256.h"
#include "xxhasher.h"

namespace xstd {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ArchiveReader::ArchiveReader(const std::filesystem::path& path,
                             ArchiveReaderOptions         opts)
    : path_(path), opts_(std::move(opts)) {}

// ---------------------------------------------------------------------------
// Open
// ---------------------------------------------------------------------------

XSTD_Result ArchiveReader::Open() {
    try {
        io_.emplace(path_, IOHandler::OpenMode::ReadOnly);
    } XSTD_ERROR_CATCH_HANDLE(kCannotOpenFile)

    try {
        ReadAndValidateHeader();

        // -----------------------------------------------------------------------
        // IEncryptor is created once — reused for ALL ReadPage() calls.
        // ICompressor is resolved lazily on first ReadPage() and then cached so
        // that uniform archives avoid repeated CompressorFactory::Create() calls.
        // -----------------------------------------------------------------------
        if (header_.IsEncrypted()) {
            if (opts_.key.empty())
                return XSTD_returnError(kMissingEncryptionKey);
            encryptor_ = EncryptorFactory::Create(header_.encryption.GetAlgorithm());
            if (!encryptor_)
                return XSTD_returnError(kUnsupportedAlgorithm);
        }

        ReadAndValidateCatalog();
    } catch (const XstdError& e) {
        io_.reset();
        return XSTD_returnError(e.code());
    }

    return XSTD_returnSuccess();
}

XSTD_Result ArchiveReader::Close() {
    compressor_.reset();
    encryptor_.reset();
    lru_cache_.clear();
    lru_order_.clear();
    io_.reset();
    return XSTD_returnSuccess();
}

// ---------------------------------------------------------------------------
// List operations
// ---------------------------------------------------------------------------

std::vector<std::string> ArchiveReader::ListFiles() const {
    std::vector<std::string> paths;
    catalog_.ScanAll([&](const std::string& k, const FileMetadata& v) {
        if (!v.deleted) paths.push_back(k);
    });
    return paths;
}

std::vector<std::string> ArchiveReader::ListDeletedFiles() const {
    std::vector<std::string> paths;
    catalog_.ScanAll([&](const std::string& k, const FileMetadata& v) {
        if (v.deleted) paths.push_back(k);
    });
    return paths;
}

std::vector<std::string> ArchiveReader::ListDirectory(const std::string& prefix) const {
    auto entries = catalog_.ListDirectory(prefix);
    std::vector<std::string> paths;
    paths.reserve(entries.size());
    for (auto& [k, v] : entries) paths.push_back(k);
    return paths;
}

std::optional<FileMetadata> ArchiveReader::Stat(const std::string& filename) const {
    return catalog_.Find(filename);
}

// ---------------------------------------------------------------------------
// Extract
// ---------------------------------------------------------------------------

XSTD_Result ArchiveReader::ExtractFile(const std::string& filename,
                                    std::vector<uint8_t>& out)
{
    auto opt_meta = catalog_.Find(filename);
    if (!opt_meta || opt_meta->deleted)
        return XSTD_returnError(kFileNotFound);

    try {
        out = AssembleFile(*opt_meta);
    } XSTD_ERROR_CATCH_HANDLE(kIOError)

    return XSTD_returnSuccess();
}

XSTD_Result ArchiveReader::ExtractFileToDisk(const std::string& filename,
                                   const std::filesystem::path& dest)
{
    std::vector<uint8_t> data;
    if (auto err = ExtractFile(filename, data); err != kSuccess)
        return err;

    // Ensure parent directories exist.
    if (dest.has_parent_path())
        std::filesystem::create_directories(dest.parent_path());

    std::ofstream out(dest, std::ios::binary);
    if (!out.is_open())
        return XSTD_returnError(kCannotWriteFile);

    try {
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    } catch (...) {
        return XSTD_returnError(kIOError);
    }

    return XSTD_returnSuccess();
}

// ---------------------------------------------------------------------------
// Private — ReadAndValidateHeader
// ---------------------------------------------------------------------------

void ArchiveReader::ReadAndValidateHeader() {
    auto span = io_->ReadAt(0, sizeof(header_));
    std::memcpy(&header_, span.data(), sizeof(header_));
    if (header_.magic != kMagic)
        XSTD_THROW_ERROR_MSG(kInvalidArchive, "invalid magic number — not an Xstd archive");
}

// ---------------------------------------------------------------------------
// Private — ReadAndValidateCatalog
// ---------------------------------------------------------------------------

void ArchiveReader::ReadAndValidateCatalog() {
    // Determine the on-disk size of the footer (possibly encrypted).
    std::unique_ptr<IEncryptor> footer_enc;
    std::size_t footer_stored_size = sizeof(ArchiveFooter);

    if (header_.encryption.IsEncrypted()) {
        // Reuse the already-created IEncryptor (stored in encryptor_) if present;
        // fall back to a temporary if not yet initialised (shouldn't happen).
        if (encryptor_) {
            footer_stored_size = encryptor_->IvSize() + sizeof(ArchiveFooter)
                                 + encryptor_->TagSize();
        } else {
            footer_enc = EncryptorFactory::Create(header_.encryption.GetAlgorithm());
            if (!footer_enc)
                XSTD_THROW_ERROR_MSG(kUnsupportedAlgorithm,
                    "unsupported encryption algorithm in archive header");
            footer_stored_size = footer_enc->IvSize() + sizeof(ArchiveFooter)
                                 + footer_enc->TagSize();
        }
    }

    // Read footer via memory-mapped view.
    const int64_t footer_offset =
        io_->FileSize() - static_cast<int64_t>(footer_stored_size);
    if (footer_offset < static_cast<int64_t>(kArchiveHeaderSize))
        XSTD_THROW_ERROR_MSG(kArchiveTruncated,
            "archive is too small to contain a valid footer");

    auto footer_span = io_->ReadAt(footer_offset, footer_stored_size);

    // Decrypt footer if needed.
    ArchiveFooter footer{};
    IEncryptor*   dec = encryptor_ ? encryptor_.get() : footer_enc.get();
    if (dec) {
        if (opts_.key.empty())
            XSTD_THROW_ERROR_MSG(kMissingEncryptionKey,
                "archive is encrypted but no decryption key was provided");
        std::vector<uint8_t> plain = dec->Decrypt(
            {footer_span.data(), footer_span.size()}, opts_.key);
        if (plain.size() != sizeof(ArchiveFooter))
            XSTD_THROW_ERROR_MSG(kDecryptionFailed,
                "decrypted footer has unexpected size — key may be wrong");
        std::memcpy(&footer, plain.data(), sizeof(ArchiveFooter));
    } else {
        std::memcpy(&footer, footer_span.data(), sizeof(ArchiveFooter));
    }

    if (footer.footer_magic != kFooterMagic)
        XSTD_THROW_ERROR_MSG(kInvalidArchive,
            "invalid footer magic — archive may be truncated or corrupted");

    // Read catalog bytes via memory-mapped view.
    if (footer.catalog_offset < 0 || footer.catalog_size <= 0)
        XSTD_THROW_ERROR_MSG(kCatalogCorrupted, "invalid catalog region in footer");

    auto cat_span = io_->ReadAt(footer.catalog_offset,
                                static_cast<std::size_t>(footer.catalog_size));
    std::vector<uint8_t> cat_buf(cat_span.begin(), cat_span.end());
    catalog_.Deserialise(cat_buf);

    if (static_cast<std::size_t>(footer.num_files) != catalog_.FileCount())
        XSTD_THROW_ERROR_MSG(kCatalogCorrupted,
            "catalog file count mismatch — archive may be corrupted");
}

// ---------------------------------------------------------------------------
// Private — ReadPage (with LRU cache)
// ---------------------------------------------------------------------------

const std::vector<uint8_t>& ArchiveReader::ReadPage(const PageHeader& ph)
{
    const CacheKey key = ph.page_id;

    // Cache hit — move to front and return.
    auto it = lru_cache_.find(key);
    if (it != lru_cache_.end()) {
        lru_order_.splice(lru_order_.begin(), lru_order_, it->second);
        return it->second->value;
    }

    // Cache miss — read from memory-mapped file (zero-copy).
    const std::size_t data_offset =
        static_cast<std::size_t>(ph.offset) + kPageHeaderSize;
    auto raw_span = io_->ReadAt(static_cast<int64_t>(data_offset),
                                static_cast<std::size_t>(ph.compressed_size));

    // Decrypt if needed, then verify CRC.
    // CRC is always computed over the compressed plaintext (post-decryption).
    std::vector<uint8_t> plaintext;
    if (ph.IsEncrypted()) {
        if (opts_.key.empty())
            XSTD_THROW_ERROR_MSG(kMissingEncryptionKey,
                "page is encrypted but no decryption key was provided");
        if (!encryptor_)
            XSTD_THROW_ERROR_MSG(kUnsupportedAlgorithm,
                "no encryptor available for page_id=" + std::to_string(ph.page_id));
        // IEncryptor::Decrypt is stateless (IV is embedded in ciphertext)
        // so the shared encryptor_ is safe to call from multiple contexts.
        plaintext = encryptor_->Decrypt(
            {raw_span.data(), raw_span.size()}, opts_.key);
    } else {
        plaintext.assign(raw_span.begin(), raw_span.end());
    }

    // Verify CRC over compressed plaintext.
    {
        const uint64_t expected_crc = static_cast<uint64_t>(ph.crc32);
        const uint64_t actual_crc   =
            XXHasher::Hash(plaintext.data(), plaintext.size()) & 0xFFFF'FFFF;
        if (actual_crc != expected_crc)
            XSTD_THROW_ERROR_MSG(kChecksumMismatch,
                "CRC mismatch on page_id=" + std::to_string(ph.page_id));
    }

    // Use the cached archive-level compressor if the codec matches; otherwise
    // create a temporary one.  This avoids repeated factory calls for uniform
    // archives while still supporting per-page variable codec (future use).
    ICompressor* comp_ptr = nullptr;
    CompressionCodec effective_codec(ph.compression_codec);

    if (compressor_ && compressor_->Codec() == effective_codec) {
        comp_ptr = compressor_.get();
    } else {
        // Cache the newly created compressor as the archive-level one
        // so the next page benefits if the codec is consistent.
        compressor_ = CompressorFactory::Create(effective_codec);
        comp_ptr    = compressor_.get();
    }

    // Decompress.
    std::vector<uint8_t> decompressed;
    XSTD_Result result = comp_ptr->Decompress(plaintext, decompressed,
                                              ph.uncompressed_size);
    if (XSTD_isError(result))
        XSTD_THROW_ERROR_MSG(kDecompressionFailed,
            "decompression failed for page_id=" + std::to_string(ph.page_id));

    // Evict LRU entry if at capacity.
    if (lru_cache_.size() >= opts_.cache_capacity && opts_.cache_capacity > 0) {
        const CacheKey evict_key = lru_order_.back().key;
        lru_cache_.erase(evict_key);
        lru_order_.pop_back();
    }

    // Insert at front.
    lru_order_.push_front(LruEntry{key, std::move(decompressed)});
    lru_cache_[key] = lru_order_.begin();
    return lru_order_.front().value;
}

// ---------------------------------------------------------------------------
// Private — AssembleFile
// ---------------------------------------------------------------------------

std::vector<uint8_t> ArchiveReader::AssembleFile(const FileMetadata& meta) {
    std::vector<uint8_t> result;
    result.reserve(static_cast<std::size_t>(meta.original_size));

    for (const PageHeader& ph : meta.pages) {
        const std::vector<uint8_t>& page_data = ReadPage(ph);
        result.insert(result.end(), page_data.begin(), page_data.end());
    }

    // Verify SHA-256 checksum.
    auto computed = Sha256::Hash(result);
    if (computed != meta.checksum)
        XSTD_THROW_ERROR_MSG(kChecksumMismatch,
            "SHA-256 mismatch for '" + meta.file_name + "' — data may be corrupted");

    return result;
}

// ---------------------------------------------------------------------------
// RecoverFile
// ---------------------------------------------------------------------------

std::optional<std::vector<uint8_t>> ArchiveReader::RecoverFile(
    const std::string& filename)
{
    auto opt_meta = catalog_.Find(filename);
    if (!opt_meta || !opt_meta->deleted) return std::nullopt;
    try {
        return AssembleFile(*opt_meta);
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace xstd
