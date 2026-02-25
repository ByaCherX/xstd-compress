#pragma once

// ---------------------------------------------------------------------------
// catalog.h — File catalog backed by a B+ Tree.
//
// The catalog maps UTF-8 file paths → FileMetadata.
// It is stored in the archive's catalog region using a compact binary format.
//
// Serialisation format for FileMetadata:
//   [uint32 file_name_len][file_name bytes]
//   [int64  signature]
//   [uint8[32] checksum]
//   [int64  created_time]
//   [int64  last_modified_time]
//   [int64  original_size]
//   [uint32 num_pages]
//     For each page: (see PageHeader — kPageHeaderSize bytes, raw packed)
//
// The string key is serialised as: [uint32 len][bytes].
// ---------------------------------------------------------------------------

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "constants.h"
#include "metadata.h"
#include "btree.h"

namespace xstd {

class Catalog {
public:
    /// Max keys per B+ Tree node.
    static constexpr int kOrder = kBTreeDefaultOrder;

    using Tree = BTree<std::string, FileMetadata, kOrder>;

    // -----------------------------------------------------------------------
    // Modification
    // -----------------------------------------------------------------------

    /// Insert or overwrite a file entry.
    void Insert(const std::string& path, const FileMetadata& meta) {
        tree_.Insert(path, meta);
    }

    /// Remove a file entry. Returns false if not found.
    bool Erase(const std::string& path) {
        return tree_.Erase(path);
    }

    // -----------------------------------------------------------------------
    // Query
    // -----------------------------------------------------------------------

    /// Exact-match lookup.
    [[nodiscard]] std::optional<FileMetadata> Find(const std::string& path) const {
        return tree_.Find(path);
    }

    /// Returns all entries whose path starts with @p prefix (e.g. a directory prefix).
    [[nodiscard]] std::vector<std::pair<std::string, FileMetadata>>
    ListDirectory(const std::string& prefix) const {
        return tree_.PrefixScanCollect(prefix);
    }

    [[nodiscard]] std::size_t FileCount() const noexcept { return tree_.Size(); }
    [[nodiscard]] bool        Empty()     const noexcept { return tree_.Empty(); }

    /// Iterates all entries in sorted key order.
    void ScanAll(std::function<void(const std::string&, const FileMetadata&)> visitor) const {
        tree_.ScanAll(std::move(visitor));
    }

    /// Collects all file paths in sorted order.
    void ScanAllPaths(std::vector<std::string>& out) const {
        tree_.ScanAll([&](const std::string& k, const FileMetadata&){ out.push_back(k); });
    }

    // -- Serialisation --
    [[nodiscard]] std::vector<uint8_t> Serialise() const;
    void Deserialise(std::span<const uint8_t> data);

private:
    Tree tree_;
};

} // namespace xstd
