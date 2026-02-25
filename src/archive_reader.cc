#include "archive_reader.h"

#include <fstream>
#include <stdexcept>
#include <string>

#include "sha256.h"

#define XXH_INLINE_ALL
#include "packages/xxhash/xxhash.h"

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

void ArchiveReader::Open() {
    file_.open(path_, std::ios::binary);
    if (!file_.is_open())
        throw std::runtime_error("ArchiveReader: cannot open file: " + path_.string());

    ReadAndValidateHeader();
    ReadAndValidateCatalog();
}

// ---------------------------------------------------------------------------
// List operations
// ---------------------------------------------------------------------------

std::vector<std::string> ArchiveReader::ListFiles() const {
    std::vector<std::string> paths;
    catalog_.ScanAllPaths(paths);
    return paths;
}

std::vector<std::string> ArchiveReader::ListDirectory(const std::string& prefix) const {
    auto entries = catalog_.ListDirectory(prefix);
    std::vector<std::string> paths;
    paths.reserve(entries.size());
    for (auto& [k, v] : entries) paths.push_back(k);
    return paths;
}

std::optional<FileMetadata> ArchiveReader::Stat(const std::string& archive_path) const {
    return catalog_.Find(archive_path);
}

// ---------------------------------------------------------------------------
// Extract
// ---------------------------------------------------------------------------

std::vector<uint8_t> ArchiveReader::ExtractFile(const std::string& archive_path) {
    auto opt_meta = catalog_.Find(archive_path);
    if (!opt_meta)
        throw std::runtime_error("ArchiveReader: file not found: " + archive_path);
    return AssembleFile(*opt_meta);
}

void ArchiveReader::ExtractFileToDisk(const std::string&           archive_path,
                                      const std::filesystem::path& dest)
{
    auto data = ExtractFile(archive_path);

    // Ensure parent directories exist.
    if (dest.has_parent_path())
        std::filesystem::create_directories(dest.parent_path());

    std::ofstream out(dest, std::ios::binary);
    if (!out.is_open())
        throw std::runtime_error("ArchiveReader: cannot write to: " + dest.string());
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
}

// ---------------------------------------------------------------------------
// Private — ReadAndValidateHeader
// ---------------------------------------------------------------------------

void ArchiveReader::ReadAndValidateHeader() {
    file_.seekg(0, std::ios::beg);
    file_.read(reinterpret_cast<char*>(&header_), sizeof(header_));
    if (file_.gcount() != sizeof(header_))
        throw std::runtime_error("ArchiveReader: cannot read archive header");
    if (header_.magic != kMagic)
        throw std::runtime_error("ArchiveReader: invalid magic number — not an Xstd archive");
}

// ---------------------------------------------------------------------------
// Private — ReadAndValidateCatalog
// ---------------------------------------------------------------------------

void ArchiveReader::ReadAndValidateCatalog() {
    // Read footer from end of file.
    file_.seekg(-static_cast<std::streamoff>(sizeof(ArchiveFooter)), std::ios::end);
    ArchiveFooter footer{};
    file_.read(reinterpret_cast<char*>(&footer), sizeof(footer));
    if (file_.gcount() != sizeof(footer))
        throw std::runtime_error("ArchiveReader: cannot read archive footer");
    if (footer.footer_magic != kFooterMagic)
        throw std::runtime_error("ArchiveReader: invalid footer magic — archive may be truncated");

    // Read catalog bytes.
    file_.seekg(footer.catalog_offset, std::ios::beg);
    std::vector<uint8_t> cat_buf(static_cast<std::size_t>(footer.catalog_size));
    file_.read(reinterpret_cast<char*>(cat_buf.data()),
               static_cast<std::streamsize>(cat_buf.size()));
    if (file_.gcount() != static_cast<std::streamsize>(cat_buf.size()))
        throw std::runtime_error("ArchiveReader: catalog region truncated");

    catalog_.Deserialise(cat_buf);

    if (static_cast<std::size_t>(footer.num_files) != catalog_.FileCount())
        throw std::runtime_error("ArchiveReader: catalog file count mismatch");
}

// ---------------------------------------------------------------------------
// Private — ReadPage (with LRU cache)
// ---------------------------------------------------------------------------

const std::vector<uint8_t>& ArchiveReader::ReadPage(const PageHeader& ph) {
    const CacheKey key = ph.page_id;

    // Cache hit — move to front and return.
    auto it = lru_cache_.find(key);
    if (it != lru_cache_.end()) {
        lru_order_.splice(lru_order_.begin(), lru_order_, it->second);
        return it->second->value;
    }

    // Cache miss — read from disk.
    file_.seekg(ph.offset + static_cast<std::streamoff>(kPageHeaderSize), std::ios::beg);
    std::vector<uint8_t> raw(static_cast<std::size_t>(ph.compressed_size));
    file_.read(reinterpret_cast<char*>(raw.data()),
               static_cast<std::streamsize>(raw.size()));
    if (file_.gcount() != static_cast<std::streamsize>(raw.size()))
        throw std::runtime_error("ArchiveReader: page data truncated (page_id="
                                 + std::to_string(ph.page_id) + ")");

    // Verify CRC (over ciphertext/compressed-plaintext before decryption).
    const uint64_t expected_crc = static_cast<uint64_t>(ph.crc32);
    const uint64_t actual_crc   = XXH64(raw.data(), raw.size(), kPageHashSeed) & 0xFFFF'FFFF;
    if (actual_crc != expected_crc)
        throw std::runtime_error("ArchiveReader: CRC mismatch on page_id="
                                 + std::to_string(ph.page_id));

    // Decrypt if needed.
    std::vector<uint8_t> plaintext;
    if (ph.IsEncrypted()) {
        if (opts_.key.empty())
            throw std::runtime_error("ArchiveReader: page is encrypted but no key provided");
        const EncryptionAlgorithm alg =
            static_cast<EncryptionAlgorithm>(header_.encryption_alg);
        auto enc = EncryptorFactory::Create(alg);
        if (!enc)
            throw std::runtime_error("ArchiveReader: unsupported encryption algorithm");
        plaintext = enc->Decrypt(raw, opts_.key);
    } else {
        plaintext = std::move(raw);
    }

    // Decompress.
    CompressionCodec codec;
    codec.raw = ph.compression_codec;
    auto comp = CompressorFactory::Create(codec);
    std::vector<uint8_t> decompressed;
    comp->Decompress(plaintext, decompressed, ph.uncompressed_size);

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
        throw std::runtime_error("ArchiveReader: SHA-256 mismatch for file: "
                                 + meta.file_name + " — data may be corrupted");

    return result;
}

} // namespace xstd
