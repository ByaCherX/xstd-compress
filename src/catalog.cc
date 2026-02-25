#include "catalog.h"

#include <cstring>
#include <stdexcept>

namespace xstd {
namespace {

// ---------------------------------------------------------------------------
// Serialisation helpers (little-endian)
// ---------------------------------------------------------------------------

inline void WriteInt64(std::vector<uint8_t>& buf, int64_t v) {
    uint64_t u = static_cast<uint64_t>(v);
    for (int i = 0; i < 8; ++i) buf.push_back(static_cast<uint8_t>(u >> (i * 8)));
}

inline int64_t ReadInt64(const uint8_t* p) {
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) u |= static_cast<uint64_t>(p[i]) << (i * 8);
    return static_cast<int64_t>(u);
}

// Serialise string key: [uint32 len][bytes]
void SerialiseKey(std::vector<uint8_t>& buf, const std::string& key) {
    detail::WriteUint32(buf, static_cast<uint32_t>(key.size()));
    buf.insert(buf.end(), key.begin(), key.end());
}

// Deserialise string key from @p p, advance @p p by bytes consumed, up to @p end.
std::string DeserialiseKey(const uint8_t*& p, const uint8_t* end) {
    if (p + 4 > end) throw std::runtime_error("Catalog: truncated key length");
    uint32_t len = detail::ReadUint32(p); p += 4;
    if (p + len > end) throw std::runtime_error("Catalog: truncated key data");
    std::string key(reinterpret_cast<const char*>(p), len);
    p += len;
    return key;
}

// Serialise FileMetadata value.
// Format:
//   [uint32 file_name_len][file_name bytes]
//   [int64  signature]
//   [uint8[32] checksum]
//   [int64  created_time]
//   [int64  last_modified_time]
//   [int64  original_size]
//   [uint32 num_pages]
//     For each page: kPageHeaderSize bytes (raw packed struct)
void SerialiseValue(std::vector<uint8_t>& buf, const FileMetadata& m) {
    // file_name
    detail::WriteUint32(buf, static_cast<uint32_t>(m.file_name.size()));
    buf.insert(buf.end(), m.file_name.begin(), m.file_name.end());

    // signature
    WriteInt64(buf, m.signature);

    // checksum (32 bytes)
    buf.insert(buf.end(), m.checksum.begin(), m.checksum.end());

    // timestamps + original_size
    WriteInt64(buf, m.created_time);
    WriteInt64(buf, m.last_modified_time);
    WriteInt64(buf, m.original_size);

    // pages
    detail::WriteUint32(buf, static_cast<uint32_t>(m.pages.size()));
    for (const PageHeader& ph : m.pages) {
        const auto* raw = reinterpret_cast<const uint8_t*>(&ph);
        buf.insert(buf.end(), raw, raw + kPageHeaderSize);
    }
}

FileMetadata DeserialiseValue(const uint8_t*& p, const uint8_t* end) {
    FileMetadata m;

    // file_name
    if (p + 4 > end) throw std::runtime_error("Catalog: truncated file_name length");
    uint32_t fn_len = detail::ReadUint32(p); p += 4;
    if (p + fn_len > end) throw std::runtime_error("Catalog: truncated file_name");
    m.file_name.assign(reinterpret_cast<const char*>(p), fn_len);
    p += fn_len;

    // signature
    if (p + 8 > end) throw std::runtime_error("Catalog: truncated signature");
    m.signature = ReadInt64(p); p += 8;

    // checksum
    if (p + kSha256Size > end) throw std::runtime_error("Catalog: truncated checksum");
    std::copy(p, p + kSha256Size, m.checksum.begin());
    p += kSha256Size;

    // timestamps + original_size
    if (p + 24 > end) throw std::runtime_error("Catalog: truncated timestamps");
    m.created_time       = ReadInt64(p); p += 8;
    m.last_modified_time = ReadInt64(p); p += 8;
    m.original_size      = ReadInt64(p); p += 8;

    // pages
    if (p + 4 > end) throw std::runtime_error("Catalog: truncated num_pages");
    uint32_t num_pages = detail::ReadUint32(p); p += 4;
    m.pages.resize(num_pages);
    for (uint32_t i = 0; i < num_pages; ++i) {
        if (p + kPageHeaderSize > end)
            throw std::runtime_error("Catalog: truncated PageHeader");
        std::memcpy(&m.pages[i], p, kPageHeaderSize);
        p += kPageHeaderSize;
    }
    return m;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Catalog::Serialise / Deserialise
// ---------------------------------------------------------------------------

std::vector<uint8_t> Catalog::Serialise() const {
    std::vector<uint8_t> buf;
    // Use BTree::Serialise with our key/value serialisers.
    tree_.Serialise(buf,
        [](std::vector<uint8_t>& b, const std::string& k)     { SerialiseKey(b, k);   },
        [](std::vector<uint8_t>& b, const FileMetadata& v) { SerialiseValue(b, v); });
    return buf;
}

void Catalog::Deserialise(std::span<const uint8_t> data) {
    tree_.Deserialise(data,
        [](const uint8_t*& p, const uint8_t* end) { return DeserialiseKey(p, end);   },
        [](const uint8_t*& p, const uint8_t* end) { return DeserialiseValue(p, end); });
}

} // namespace xstd
