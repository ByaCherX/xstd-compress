#include "archive_writer.h"

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>

#include "sha256.h"

#define XXH_INLINE_ALL
#include "packages/xxhash/xxhash.h"

namespace xstd {

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

ArchiveWriter::ArchiveWriter(const std::filesystem::path& path,
                             ArchiveWriterOptions         opts)
    : opts_(std::move(opts))
{
    ValidateOptions();

    file_.open(path, std::ios::binary | std::ios::trunc);
    if (!file_.is_open())
        throw std::runtime_error("ArchiveWriter: cannot open file: " + path.string());

    compressor_ = CompressorFactory::Create(opts_.codec);
    if (opts_.encryption != EncryptionAlgorithm::NONE)
        encryptor_ = EncryptorFactory::Create(opts_.encryption);

    WriteArchiveHeader();
}

ArchiveWriter::~ArchiveWriter() {
    if (!finalised_ && file_.is_open()) {
        // Best-effort silent finalise on destruction to avoid partially-written archives.
        try { Finalise(); } catch (...) {}
    }
}

// ---------------------------------------------------------------------------
// AddFile
// ---------------------------------------------------------------------------

void ArchiveWriter::AddFile(const std::string&       archive_path,
                            std::span<const uint8_t> data)
{
    if (finalised_)
        throw std::runtime_error("ArchiveWriter: cannot add files after Finalise()");

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
}

// ---------------------------------------------------------------------------
// AddFileFromDisk
// ---------------------------------------------------------------------------

void ArchiveWriter::AddFileFromDisk(const std::filesystem::path& source,
                                    const std::string&           archive_path)
{
    std::ifstream f(source, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("ArchiveWriter: cannot open source file: "
                                 + source.string());
    std::vector<uint8_t> data(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
    AddFile(archive_path, data);
}

// ---------------------------------------------------------------------------
// Finalise
// ---------------------------------------------------------------------------

void ArchiveWriter::Finalise() {
    if (finalised_) return;
    finalised_ = true;

    // Serialize catalog.
    std::vector<uint8_t> cat_buf = catalog_.Serialise();

    const int64_t catalog_offset = static_cast<int64_t>(file_.tellp());
    file_.write(reinterpret_cast<const char*>(cat_buf.data()),
                static_cast<std::streamsize>(cat_buf.size()));

    // Write footer.
    ArchiveFooter footer;
    footer.catalog_offset = catalog_offset;
    footer.catalog_size   = static_cast<int64_t>(cat_buf.size());
    footer.num_files      = static_cast<int64_t>(file_count_);

    file_.write(reinterpret_cast<const char*>(&footer), sizeof(footer));
    file_.flush();
    file_.close();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ArchiveWriter::ValidateOptions() const {
    if (opts_.encryption != EncryptionAlgorithm::NONE) {
        const std::size_t expected = static_cast<std::size_t>(opts_.key_size);
        if (opts_.key.size() != expected)
            throw std::invalid_argument(
                "ArchiveWriter: key size does not match key_size option");
    }
}

void ArchiveWriter::WriteArchiveHeader() {
    ArchiveHeader hdr;
    hdr.page_size      = static_cast<uint8_t>(opts_.page_size);
    hdr.encryption_alg = static_cast<uint8_t>(opts_.encryption);
    hdr.aes_key_size   = static_cast<uint8_t>(opts_.key_size);
    hdr.num_pages      = 0;  // will not be patched back; use footer's num_files instead
    if (opts_.encryption != EncryptionAlgorithm::NONE) hdr.SetEncrypted(true);

    file_.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
}

PageHeader ArchiveWriter::WritePage(std::span<const uint8_t> chunk,
                                    PageType                  type)
{
    // 1. Compress.
    std::vector<uint8_t> compressed;
    compressor_->Compress(chunk, compressed);

    // 2. CRC of compressed plaintext.
    const uint64_t crc = XXH64(compressed.data(), compressed.size(), kPageHashSeed);

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
