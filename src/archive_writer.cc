#include "archive_writer.h"

#include <chrono>
#include <cstddef>
#include <fstream>
#include <stdexcept>
#include <string>

#include "sha256.h"
#include "xxhasher.h"

namespace xstd {

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

ArchiveWriter::ArchiveWriter(const std::filesystem::path& path,
                             ArchiveWriterOptions         opts)
    : path_(path), opts_(std::move(opts))
{}

ArchiveWriter::~ArchiveWriter() {
    if (!finalised_ && io_.has_value())
        (void)Finalise();
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

XSTD_Result ArchiveWriter::Init() {
    try {
        ValidateOptions();
    } XSTD_ERROR_CATCH_HANDLE(kInvalidArgument)

    try {
        io_.emplace(path_, IOHandler::OpenMode::WriteOnly);
    } XSTD_ERROR_CATCH_HANDLE(kCannotOpenFile)

    try {
        compressor_ = CompressorFactory::Create(opts_.codec);
        if (opts_.encryption.IsEncrypted())
            encryptor_ = EncryptorFactory::Create(opts_.encryption.GetAlgorithm());

        WriteArchiveHeader();
    } catch (const XstdError& e) {
        io_.reset();
        return XSTD_returnError(e.code());
    } catch (...) {
        io_.reset();
        return XSTD_returnError(kIOError);
    }

    return XSTD_returnSuccess();
}

// ---------------------------------------------------------------------------
// AddFile
// ---------------------------------------------------------------------------

XSTD_Result ArchiveWriter::AddFile(const std::string&       filename,
                                   std::span<const uint8_t> data)
{
    if (finalised_)
        return XSTD_returnError(kAlreadyFinalised);

    try {
        // Compute SHA-256 of original data.
        auto checksum = Sha256::Hash(data);

        // Record wall-clock timestamps.
        const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        FileMetadata meta;
        meta.file_name         = filename;
        meta.original_size     = static_cast<int64_t>(data.size());
        meta.created_time      = now;
        meta.last_modified_time = now;
        std::copy(checksum.begin(), checksum.end(), meta.checksum.begin());

        // Split data into page-sized chunks and write.
        const uint32_t page_bytes = PageSizeBytes(opts_.page_size);
        std::size_t    offset     = 0;

        while (offset < data.size() || data.empty()) {
            const std::size_t chunk_size = std::min<std::size_t>(
                page_bytes, data.size() - offset);
            std::span<const uint8_t> chunk(data.data() + offset, chunk_size);

            // Using default compressor for the page; override with per_page_codec if needed.
            PageHeader ph = WritePage(chunk);
            meta.pages.push_back(ph);

            offset += chunk_size;
            if (data.empty()) break;  // single empty-file page
        }

        catalog_.Insert(filename, meta);
        ++file_count_;
    } XSTD_ERROR_CATCH_HANDLE(kIOError)

    return XSTD_returnSuccess();
}

// ---------------------------------------------------------------------------
// AddFileFromDisk
// ---------------------------------------------------------------------------

XSTD_Result ArchiveWriter::AddFileFromDisk(const std::filesystem::path& source,
                                           const std::string&           filename)
{
    std::ifstream f(source, std::ios::binary);
    if (!f.is_open())
        return XSTD_returnError(kCannotOpenFile);

    std::vector<uint8_t> data(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
    return AddFile(filename, data);
}

// ---------------------------------------------------------------------------
// Finalise
// ---------------------------------------------------------------------------

XSTD_Result ArchiveWriter::Finalise() {
    if (finalised_) return XSTD_returnSuccess();
    finalised_ = true;

    try {
        // Serialize catalog.
        std::vector<uint8_t> cat_buf = catalog_.Serialise();

        const int64_t catalog_offset = io_->AppendPosition();
        if (auto r = io_->Append({cat_buf.data(), cat_buf.size()}); XSTD_isError(r))
            return r;

        // Build footer struct.
        ArchiveFooter footer;
        footer.catalog_offset = catalog_offset;
        footer.catalog_size   = static_cast<int64_t>(cat_buf.size());
        footer.num_files      = static_cast<int64_t>(file_count_);

        // Write footer — encrypted when an encryptor is active.
        if (encryptor_) {
            const auto* raw = reinterpret_cast<const uint8_t*>(&footer);
            auto enc_footer = encryptor_->Encrypt(
                std::span<const uint8_t>(raw, sizeof(footer)), opts_.key);
            if (auto r = io_->Append({enc_footer.data(), enc_footer.size()});
                XSTD_isError(r))
                return r;
        } else {
            const auto* raw = reinterpret_cast<const uint8_t*>(&footer);
            if (auto r = io_->Append({raw, sizeof(footer)}); XSTD_isError(r))
                return r;
        }

        if (auto r = io_->Flush(); XSTD_isError(r)) return r;
        io_.reset();
    } catch (...) {
        return XSTD_returnError(kIOError);
    }

    return XSTD_returnSuccess();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ArchiveWriter::ValidateOptions() const {
    if (opts_.encryption.IsEncrypted()) {
        const std::size_t expected_key_bytes =
            static_cast<std::size_t>(opts_.encryption.GetKeySize());
        if (opts_.key.size() != expected_key_bytes)
            XSTD_THROW_ERROR_MSG(kInvalidArgument,
                "encryption key must be " + std::to_string(expected_key_bytes)
                + " bytes but got " + std::to_string(opts_.key.size()));
        if (opts_.encryption.GetAlgorithm() == EncryptionAlgorithm::NONE)
            XSTD_THROW_ERROR_MSG(kInvalidArgument,
                "encryption algorithm is NONE but the encrypted flag is set");
    }
}

void ArchiveWriter::WriteArchiveHeader() {
    ArchiveHeader hdr;
    hdr.page_size  = static_cast<uint8_t>(opts_.page_size);
    hdr.encryption = opts_.encryption;
    hdr.flags.SetCompressed(opts_.codec.Type() != CompressionType::UNCOMPRESSED);
    hdr.num_pages  = 0;

    const auto* raw = reinterpret_cast<const uint8_t*>(&hdr);
    XSTD_Result r   = io_->Append({raw, sizeof(hdr)});
    if (XSTD_isError(r))
        XSTD_THROW_ERROR_MSG(kCannotWriteFile, "failed to write archive header");
}

XSTD_Result ArchiveWriter::DeleteFile(const std::string& filename) {
    if (finalised_)
        return XSTD_returnError(kAlreadyFinalised);

    auto opt_meta = catalog_.Find(filename);
    if (!opt_meta) return XSTD_returnError(kFileNotFound);
    if (opt_meta->deleted) return XSTD_returnSuccess();  // already deleted

    try {
        // Patch each page's flags byte on disk: set bit 1 (deleted).
        // PageHeader::flags is at byte offset 11 within the packed struct.
        constexpr std::size_t kFlagsOffset = 11;
        FileMetadata meta = *opt_meta;
        for (PageHeader& ph : meta.pages) {
            ph.SetDeleted(true);
            const int64_t patch_off =
                static_cast<int64_t>(ph.offset)
                + static_cast<int64_t>(kFlagsOffset);
            if (auto r = io_->WriteAt(patch_off, {&ph.flags, 1}); XSTD_isError(r))
                return r;
        }

        meta.deleted = true;
        catalog_.Insert(filename, meta);
        // No need to restore an EOF pointer — IOHandler tracks append_pos_
        // independently of WriteAt operations.
    } catch (...) {
        return XSTD_returnError(kIOError);
    }

    return XSTD_returnSuccess();
}

PageHeader ArchiveWriter::WritePage(std::span<const uint8_t> chunk,
                                    PageType                 type,
                                    Encoding                 encoding,
                                    CompressionCodec         per_page_codec)
{
    // Resolve effective codec: use opts_.codec as default.
    const CompressionCodec effective_codec =
        (per_page_codec.raw != 0) ? per_page_codec : opts_.codec;

    // Select compressor: reuse member if codec matches, otherwise create temporary.
    ICompressor* comp_ptr = nullptr;
    std::unique_ptr<ICompressor> tmp_comp;
    if (compressor_ && compressor_->Codec() == effective_codec) {
        comp_ptr = compressor_.get();
    } else {
        tmp_comp = CompressorFactory::Create(effective_codec);
        comp_ptr = tmp_comp.get();
    }

    // 1. Compress.
    std::vector<uint8_t> compressed;
    XSTD_Result result = comp_ptr->Compress(chunk, compressed);
    if (XSTD_isError(result))
        throw XstdError(kCompressionFailed);

    // 2. CRC of compressed plaintext.
    const uint64_t crc = XXHasher::Hash(compressed.data(), compressed.size());

    // 3. Optionally encrypt.
    std::vector<uint8_t> payload;
    bool encrypted = false;
    if (encryptor_) {
        payload   = encryptor_->Encrypt(compressed, opts_.key);
        encrypted = true;
    } else {
        payload = std::move(compressed);
    }

    // 4. Build PageHeader.
    const int32_t write_offset = static_cast<int32_t>(io_->AppendPosition());
    PageHeader ph;
    ph.page_id           = AllocatePageId();
    ph.offset            = write_offset;
    ph.page_type         = static_cast<uint8_t>(type);
    ph.encoding          = static_cast<uint8_t>(encoding);
    ph.compression_codec = effective_codec.raw;
    ph.uncompressed_size = static_cast<int32_t>(chunk.size());
    ph.compressed_size   = static_cast<int32_t>(payload.size());
    ph.crc32             = static_cast<uint32_t>(crc & 0xFFFF'FFFF);
    ph.SetEncrypted(encrypted);

    // 5. Append header + payload.
    const auto* ph_raw = reinterpret_cast<const uint8_t*>(&ph);
    if (auto io_result = io_->Append({ph_raw, sizeof(ph)});
            XSTD_isError(io_result))
        throw XstdError(kCannotWriteFile);
    if (auto io_result = io_->Append({payload.data(), payload.size()});
            XSTD_isError(io_result))
        throw XstdError(kCannotWriteFile);

    return ph;
}

int32_t ArchiveWriter::AllocatePageId() {
    return next_page_id_++;
}

} // namespace xstd



