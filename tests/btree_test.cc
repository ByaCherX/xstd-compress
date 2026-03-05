// ---------------------------------------------------------------------------
// btree_test.cc — Tests for the B+ Tree implementation.
// ---------------------------------------------------------------------------
#include <gtest/gtest.h>
#include "btree.h"
#include "catalog.h"
#include "metadata.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace xstd;

// Use a small order to exercise splits frequently.
using SmallTree = BTree<std::string, int, 4>;
using IntTree   = BTree<int, std::string, 4>;

// ---------------------------------------------------------------------------
// Basic Insert / Find
// ---------------------------------------------------------------------------
TEST(BTreeTest, InsertAndFind) {
    SmallTree t;
    t.Insert("banana", 2);
    t.Insert("apple",  1);
    t.Insert("cherry", 3);

    EXPECT_EQ(t.Find("apple").value(),  1);
    EXPECT_EQ(t.Find("banana").value(), 2);
    EXPECT_EQ(t.Find("cherry").value(), 3);
    EXPECT_FALSE(t.Find("date").has_value());
}

TEST(BTreeTest, InsertOverwrite) {
    SmallTree t;
    t.Insert("key", 1);
    t.Insert("key", 99);
    EXPECT_EQ(t.Find("key").value(), 99);
    EXPECT_EQ(t.Size(), 1u);
}

// ---------------------------------------------------------------------------
// Size tracking
// ---------------------------------------------------------------------------
TEST(BTreeTest, SizeTracking) {
    SmallTree t;
    EXPECT_EQ(t.Size(), 0u);
    t.Insert("a", 1);
    EXPECT_EQ(t.Size(), 1u);
    t.Insert("b", 2);
    EXPECT_EQ(t.Size(), 2u);
    t.Erase("a");
    EXPECT_EQ(t.Size(), 1u);
}

// ---------------------------------------------------------------------------
// Many inserts — triggers multiple splits
// ---------------------------------------------------------------------------
TEST(BTreeTest, ManySortedInsertsPreserveOrder) {
    SmallTree t;
    std::vector<std::string> words = {
        "alpha","beta","gamma","delta","epsilon","zeta","eta","theta","iota","kappa"
    };
    for (auto& w : words) t.Insert(w, static_cast<int>(w.size()));

    std::vector<std::string> scanned;
    t.ScanAll([&](const std::string& k, int){ scanned.push_back(k); });

    std::vector<std::string> sorted = words;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(scanned, sorted);
}

// ---------------------------------------------------------------------------
// Erase
// ---------------------------------------------------------------------------
TEST(BTreeTest, EraseExisting) {
    SmallTree t;
    t.Insert("x", 10);
    t.Insert("y", 20);
    EXPECT_TRUE(t.Erase("x"));
    EXPECT_FALSE(t.Find("x").has_value());
    EXPECT_EQ(t.Find("y").value(), 20);
}

TEST(BTreeTest, EraseNonExistent) {
    SmallTree t;
    t.Insert("a", 1);
    EXPECT_FALSE(t.Erase("z"));
}

// ---------------------------------------------------------------------------
// Prefix scan
// ---------------------------------------------------------------------------
TEST(BTreeTest, PrefixScan) {
    SmallTree t;
    t.Insert("src/main.cpp",  1);
    t.Insert("src/util.h",    2);
    t.Insert("src/util.cpp",  3);
    t.Insert("tests/a.cc",    4);
    t.Insert("README.md",     5);

    auto results = t.PrefixScanCollect("src/");
    ASSERT_EQ(results.size(), 3u);
    for (auto& [k, _] : results)
        EXPECT_TRUE(k.starts_with("src/"));
}

TEST(BTreeTest, PrefixScanEmpty) {
    SmallTree t;
    t.Insert("foo", 1);
    auto r = t.PrefixScanCollect("bar");
    EXPECT_TRUE(r.empty());
}

// ---------------------------------------------------------------------------
// Serialise / Deserialise round-trip
// ---------------------------------------------------------------------------
TEST(BTreeTest, SerialiseDeserialiseRoundTrip) {
    SmallTree src;
    src.Insert("apple",  1);
    src.Insert("banana", 2);
    src.Insert("cherry", 3);
    src.Insert("date",   4);
    src.Insert("elderberry", 5);

    // Serialise.
    auto ks = [](std::vector<uint8_t>& buf, const std::string& k){
        xstd::detail::WriteString(buf, k);
    };
    auto vs = [](std::vector<uint8_t>& buf, int v){
        for (int i = 0; i < 4; ++i)
            buf.push_back(static_cast<uint8_t>(v >> (i * 8)));
    };
    std::vector<uint8_t> encoded;
    src.Serialise(encoded, ks, vs);

    // Deserialise into a new tree.
    SmallTree dst;
    auto kd = [](const uint8_t*& p, const uint8_t* end) -> std::string {
        return xstd::detail::ReadString(p, end);
    };
    auto vd = [](const uint8_t*& p, const uint8_t* end) -> int {
        if (p + 4 > end) throw std::runtime_error("truncated int");
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(p[i]) << (i*8);
        p += 4;
        return static_cast<int>(v);
    };
    dst.Deserialise(encoded, kd, vd);

    EXPECT_EQ(dst.Size(), src.Size());
    EXPECT_EQ(dst.Find("apple").value(),  1);
    EXPECT_EQ(dst.Find("banana").value(), 2);
    EXPECT_EQ(dst.Find("elderberry").value(), 5);
    EXPECT_FALSE(dst.Find("fig").has_value());

    // Verify sorted order is preserved.
    std::vector<std::string> keys;
    dst.ScanAll([&](const std::string& k, int){ keys.push_back(k); });
    EXPECT_TRUE(std::is_sorted(keys.begin(), keys.end()));
}

// ---------------------------------------------------------------------------
// Catalog round-trip
// ---------------------------------------------------------------------------
TEST(CatalogTest, InsertFindErase) {
    Catalog cat;
    FileMetadata m;
    m.file_name      = "test.txt";
    m.original_size  = 42;
    cat.Insert("test.txt", m);

    auto found = cat.Find("test.txt");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->original_size, 42);

    EXPECT_TRUE(cat.Erase("test.txt"));
    EXPECT_FALSE(cat.Find("test.txt").has_value());
}

TEST(CatalogTest, SerialiseDeserialiseRoundTrip) {
    Catalog src;
    for (int i = 0; i < 10; ++i) {
        FileMetadata m;
        m.file_name     = "file_" + std::to_string(i) + ".bin";
        m.original_size = i * 1000;
        m.created_time  = 1700000000 + i;
        src.Insert(m.file_name, m);
    }

    auto bytes = src.Serialise();

    Catalog dst;
    dst.Deserialise(bytes);

    EXPECT_EQ(dst.FileCount(), src.FileCount());
    for (int i = 0; i < 10; ++i) {
        std::string path = "file_" + std::to_string(i) + ".bin";
        auto f = dst.Find(path);
        ASSERT_TRUE(f.has_value()) << "Missing: " << path;
        EXPECT_EQ(f->original_size, i * 1000);
        EXPECT_EQ(f->created_time, 1700000000 + i);
    }
}
