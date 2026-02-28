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
    if (!finalised_ && file_.is_open())
        (void)Finalise();
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

XSTD_Result ArchiveWriter::Init() {
    try {
        ValidateOptions();
    } catch (const std::invalid_argument&) {
        return XSTD_returnError(kInvalidArgument);
    }

    file_.open(path_, std::ios::binary | std::ios::trunc);
    if (!file_.is_open())
        return XSTD_returnError(kCannotOpenFile);

    try {
        compressor_ = CompressorFactory::Create(opts_.codec);
        if (opts_.encryption.IsEncrypted())
            encryptor_ = EncryptorFactory::Create(opts_.encryption.GetAlgorithm());

        WriteArchiveHeader();
    } catch (...) {
        file_.close();
        return XSTD_returnError(kIOError);
    }

    return XSTD_returnSuccess();
}

// ---------------------------------------------------------------------------
// AddFile
// ---------------------------------------------------------------------------

XSTD_Result ArchiveWriter::AddFile(const std::string&       archive_path,
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
        meta.file_name         = archive_path;
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

            PageHeader ph = WritePage(chunk, PageType::DATA_PAGE);
            meta.pages.push_back(ph);

            offset += chunk_size;
            if (data.empty()) break;  // single empty-file page
        }

        catalog_.Insert(archive_path, meta);
        ++file_count_;
    } catch (...) {
        return XSTD_returnError(kIOError);
    }

    return XSTD_returnSuccess();
}

// ---------------------------------------------------------------------------
// AddFileFromDisk
// ---------------------------------------------------------------------------

XSTD_Result ArchiveWriter::AddFileFromDisk(const std::filesystem::path& source,
                                            const std::string&           archive_path)
{
    std::ifstream f(source, std::ios::binary);
    if (!f.is_open())
        return XSTD_returnError(kCannotOpenFile);

    std::vector<uint8_t> data(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
    return AddFile(archive_path, data);
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

        const int64_t catalog_offset = static_cast<int64_t>(file_.tellp());
        file_.write(reinterpret_cast<const char*>(cat_buf.data()),
                    static_cast<std::streamsize>(cat_buf.size()));

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
            file_.write(reinterpret_cast<const char*>(enc_footer.data()),
                        static_cast<std::streamsize>(enc_footer.size()));
        } else {
            file_.write(reinterpret_cast<const char*>(&footer), sizeof(footer));
        }

        file_.flush();
        file_.close();
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
        if (opts_.key.size() >= 32 || opts_.key.size() == 0)
            throw std::invalid_argument(
                "ArchiveWriter: key size does not match the key size encoded in encryption option");
        if (opts_.encryption.GetAlgorithm() == EncryptionAlgorithm::NONE)
            throw std::invalid_argument(
                "ArchiveWriter: encryption algorithm is NONE but encryption is enabled");
    }
}

void ArchiveWriter::WriteArchiveHeader() {
    ArchiveHeader hdr;
    hdr.page_size  = static_cast<uint8_t>(opts_.page_size);
    hdr.encryption = opts_.encryption;
    hdr.flags.SetCompressed(opts_.codec.Type() != CompressionType::UNCOMPRESSED);
    hdr.num_pages  = 0;

    file_.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
}

XSTD_Result ArchiveWriter::DeleteFile(const std::string& archive_path) {
    if (finalised_)
        return XSTD_returnError(kAlreadyFinalised);

    auto opt_meta = catalog_.Find(archive_path);
    if (!opt_meta) return XSTD_returnError(kFileNotFound);
    if (opt_meta->deleted) return XSTD_returnSuccess();  // already deleted

    try {
        // Patch each page's flags byte on disk: set bit 1 (deleted).
        // PageHeader::flags is at byte offset 11 within the packed struct.
        constexpr std::size_t kFlagsOffset = 11;
        FileMetadata meta = *opt_meta;
        for (PageHeader& ph : meta.pages) {
            ph.SetDeleted(true);
            file_.seekp(static_cast<std::streamoff>(ph.offset) +
                        static_cast<std::streamoff>(kFlagsOffset),
                        std::ios::beg);
            file_.write(reinterpret_cast<const char*>(&ph.flags), 1);
        }

        meta.deleted = true;
        catalog_.Insert(archive_path, meta);

        // Restore the put pointer to end-of-file so that Finalise()
        // correctly determines the catalog offset.
        file_.seekp(0, std::ios::end);
    } catch (...) {
        return XSTD_returnError(kIOError);
    }

    return XSTD_returnSuccess();
}

PageHeader ArchiveWriter::WritePage(std::span<const uint8_t> chunk,
                                    PageType                  type)
{
    // 1. Compress.
    std::vector<uint8_t> compressed;
    compressor_->Compress(chunk, compressed);

    // 2. CRC of compressed plaintext.
    const uint64_t crc = XXHasher::Hash(compressed.data(), compressed.size());

    // 3. Optionally encrypt.
    std::vector<uint8_t> payload;
    bool encrypted = false;
    if (encryptor_) {
        payload    = encryptor_->Encrypt(compressed, opts_.key);
        encrypted  = true;
    } else {
        payload = std::move(compressed);
    }

    // 4. Build PageHeader.
    const int32_t write_offset = static_cast<int32_t>(file_.tellp());
    PageHeader ph;
    ph.page_id           = AllocatePageId();
    ph.offset            = write_offset;
    ph.page_type         = static_cast<uint8_t>(type);
    ph.encoding          = static_cast<uint8_t>(Encoding::PLAIN);
    ph.compression_codec = opts_.codec.raw;
    ph.uncompressed_size = static_cast<int32_t>(chunk.size());
    ph.compressed_size   = static_cast<int32_t>(payload.size());
    ph.crc32             = static_cast<uint32_t>(crc & 0xFFFF'FFFF);
    ph.SetEncrypted(encrypted);

    // 5. Write header + payload.
    file_.write(reinterpret_cast<const char*>(&ph), sizeof(ph));
    file_.write(reinterpret_cast<const char*>(payload.data()),
                static_cast<std::streamsize>(payload.size()));

    return ph;
}

int32_t ArchiveWriter::AllocatePageId() {
    return next_page_id_++;
}

} // namespace xstd
