// ---------------------------------------------------------------------------
// archive_test.cc — Integration tests: write → read round-trip.
// ---------------------------------------------------------------------------
#include <gtest/gtest.h>
#include "archive_writer.h"
#include "archive_reader.h"

#include <chrono>
#include <filesystem>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

using namespace xstd;
namespace fs = std::filesystem;

// RAII temp file: deleted when it goes out of scope.
struct TempFile {
    fs::path path;
    explicit TempFile(std::string suffix = ".xstd")
        : path(fs::temp_directory_path() / ("xstd_test_" +
               std::to_string(
                   std::chrono::system_clock::now().time_since_epoch().count()) + suffix)) {}
    ~TempFile() { std::error_code ec; fs::remove(path, ec); }
};

static std::vector<uint8_t> MakeData(std::string_view s) {
    return {s.begin(), s.end()};
}

static std::vector<uint8_t> AES256Key() {
    std::vector<uint8_t> k(32);
    for (uint8_t i = 0; i < 32; ++i) k[i] = i + 1;
    return k;
}

// ---------------------------------------------------------------------------
// Basic write + read
// ---------------------------------------------------------------------------
TEST(ArchiveTest, SingleFileRoundTrip) {
    TempFile tmp;
    auto data = MakeData("Hello, Xstd world!");

    {
        ArchiveWriterOptions opts;
        opts.codec = CompressionCodec{CompressionType::ZSTD, CompressionLevel::XSTD_greedy};
        ArchiveWriter w(tmp.path, opts);
        ASSERT_EQ(w.Init(), (kSuccess));
        ASSERT_EQ(w.AddFile("hello.txt", data), (kSuccess));
    }   // Finalise on destruction.

    ArchiveReader r(tmp.path);
    ASSERT_EQ(r.Open(), (kSuccess));

    EXPECT_EQ(r.FileCount(), 1u);
    auto files = r.ListFiles();
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0], "hello.txt");

    std::vector<uint8_t> out;
    ASSERT_EQ(r.ExtractFile("hello.txt", out), (kSuccess));
    EXPECT_EQ(out, data);
}

TEST(ArchiveTest, MultipleFilesRoundTrip) {
    TempFile tmp;
    std::vector<std::pair<std::string, std::vector<uint8_t>>> entries = {
        {"docs/readme.txt",  MakeData("README content")},
        {"data/input.csv",   MakeData("a,b,c\n1,2,3\n")},
        {"src/main.cpp",     MakeData("#include <iostream>")},
    };

    {
        ArchiveWriter w(tmp.path);
        ASSERT_EQ(w.Init(), (kSuccess));
        for (auto& [p, d] : entries) ASSERT_EQ(w.AddFile(p, d), (kSuccess));
    }

    ArchiveReader r(tmp.path);
    ASSERT_EQ(r.Open(), (kSuccess));
    EXPECT_EQ(r.FileCount(), 3u);

    for (auto& [p, d] : entries) {
        std::vector<uint8_t> out;
        ASSERT_EQ(r.ExtractFile(p, out), (kSuccess));
        EXPECT_EQ(out, d) << "Mismatch for " << p;
    }
}

TEST(ArchiveTest, EmptyFileRoundTrip) {
    TempFile tmp;
    {
        ArchiveWriter w(tmp.path);
        ASSERT_EQ(w.Init(), (kSuccess));
        ASSERT_EQ(w.AddFile("empty.bin", std::vector<uint8_t>{}), (kSuccess));
    }
    ArchiveReader r(tmp.path);
    ASSERT_EQ(r.Open(), (kSuccess));
    std::vector<uint8_t> out;
    ASSERT_EQ(r.ExtractFile("empty.bin", out), (kSuccess));
    EXPECT_TRUE(out.empty());
}

// ---------------------------------------------------------------------------
// Large file — spans multiple pages
// ---------------------------------------------------------------------------
TEST(ArchiveTest, LargeFileMultiplePages) {
    TempFile tmp;
    std::vector<uint8_t> big(256 * 1024 + 7);   // slightly over 4 pages at 64K
    std::iota(big.begin(), big.end(), 0u);

    ArchiveWriterOptions opts;
    opts.page_size = PageSize::PAGE_64K;
    {
        ArchiveWriter w(tmp.path, opts);
        ASSERT_EQ(w.Init(), (kSuccess));
        ASSERT_EQ(w.AddFile("large.bin", big), (kSuccess));
    }
    ArchiveReader r(tmp.path);
    ASSERT_EQ(r.Open(), (kSuccess));
    std::vector<uint8_t> out;
    ASSERT_EQ(r.ExtractFile("large.bin", out), (kSuccess));
    EXPECT_EQ(out, big);
}

// ---------------------------------------------------------------------------
// Encrypted archive (AES-GCM)
// ---------------------------------------------------------------------------
TEST(ArchiveTest, EncryptedGcmRoundTrip) {
    TempFile tmp;
    auto key = AES256Key();
    auto data = MakeData("Top secret payload!");

    {
        ArchiveWriterOptions opts;
        opts.encryption = ArchiveEncryption::Make(EncryptionAlgorithm::AES_GCM_V1, AesKeySize::AES_256);
        opts.key        = key;
        ArchiveWriter w(tmp.path, opts);
        ASSERT_EQ(w.Init(), (kSuccess));
        ASSERT_EQ(w.AddFile("secret.txt", data), (kSuccess));
    }

    ArchiveReaderOptions ropts;
    ropts.key = key;
    ArchiveReader r(tmp.path, ropts);
    ASSERT_EQ(r.Open(), (kSuccess));
    std::vector<uint8_t> out;
    ASSERT_EQ(r.ExtractFile("secret.txt", out), (kSuccess));
    EXPECT_EQ(out, data);
}

TEST(ArchiveTest, EncryptedCtrRoundTrip) {
    TempFile tmp;
    auto key = AES256Key();
    auto data = MakeData("CTR encrypted data");

    {
        ArchiveWriterOptions opts;
        opts.encryption = ArchiveEncryption::Make(EncryptionAlgorithm::AES_CTR_V1, AesKeySize::AES_256);
        opts.key        = key;
        ArchiveWriter w(tmp.path, opts);
        ASSERT_EQ(w.Init(), (kSuccess));
        ASSERT_EQ(w.AddFile("ctr.bin", data), (kSuccess));
    }

    ArchiveReaderOptions ropts;
    ropts.key = key;
    ArchiveReader r(tmp.path, ropts);
    ASSERT_EQ(r.Open(), (kSuccess));
    std::vector<uint8_t> out;
    ASSERT_EQ(r.ExtractFile("ctr.bin", out), (kSuccess));
    EXPECT_EQ(out, data);
}

TEST(ArchiveTest, EncryptedArchiveWithoutKeyReturnsError) {
    TempFile tmp;
    auto key = AES256Key();
    {
        ArchiveWriterOptions opts;
        opts.encryption = ArchiveEncryption::Make(EncryptionAlgorithm::AES_GCM_V1, AesKeySize::AES_256);
        opts.key        = key;
        ArchiveWriter w(tmp.path, opts);
        ASSERT_EQ(w.Init(), (kSuccess));
        (void)w.AddFile("x.txt", MakeData("data"));
    }
    // Open without key → should return a decryption error.
    ArchiveReader r(tmp.path);
    EXPECT_EQ(r.Open(), XSTD_returnError(kMissingEncryptionKey));
}

// ---------------------------------------------------------------------------
// Directory listing
// ---------------------------------------------------------------------------
TEST(ArchiveTest, ListDirectory) {
    TempFile tmp;
    {
        ArchiveWriter w(tmp.path);
        ASSERT_EQ(w.Init(), (kSuccess));
        (void)w.AddFile("src/a.cpp",  MakeData("a"));
        (void)w.AddFile("src/b.cpp",  MakeData("b"));
        (void)w.AddFile("docs/x.md",  MakeData("x"));
        (void)w.AddFile("README.md",  MakeData("r"));
    }
    ArchiveReader r(tmp.path);
    ASSERT_EQ(r.Open(), (kSuccess));
    auto src_files = r.ListDirectory("src/");
    ASSERT_EQ(src_files.size(), 2u);
    for (auto& f : src_files) EXPECT_TRUE(f.starts_with("src/"));
}

// ---------------------------------------------------------------------------
// Stat (file metadata query)
// ---------------------------------------------------------------------------
TEST(ArchiveTest, StatReturnsMetadata) {
    TempFile tmp;
    auto data = MakeData("content");
    {
        ArchiveWriter w(tmp.path);
        ASSERT_EQ(w.Init(), (kSuccess));
        (void)w.AddFile("file.txt", data);
    }
    ArchiveReader r(tmp.path);
    ASSERT_EQ(r.Open(), (kSuccess));
    auto m = r.Stat("file.txt");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->original_size, static_cast<int64_t>(data.size()));
    EXPECT_GT(m->created_time, 0);
}

TEST(ArchiveTest, StatMissingFileReturnsNullopt) {
    TempFile tmp;
    {
        ArchiveWriter w(tmp.path);
        ASSERT_EQ(w.Init(), (kSuccess));
        (void)w.AddFile("a.txt", MakeData("x"));
    }
    ArchiveReader r(tmp.path);
    ASSERT_EQ(r.Open(), (kSuccess));
    EXPECT_FALSE(r.Stat("missing.txt").has_value());
}

// ---------------------------------------------------------------------------
// Compression codecs
// ---------------------------------------------------------------------------
TEST(ArchiveTest, NoCompressionRoundTrip) {
    TempFile tmp;
    auto data = MakeData("uncompressed data");
    {
        ArchiveWriterOptions opts;
        opts.codec = CompressionCodec{CompressionType::UNCOMPRESSED, CompressionLevel::XSTD_RESERVED_LEVEL};
        ArchiveWriter w(tmp.path, opts);
        ASSERT_EQ(w.Init(), (kSuccess));
        (void)w.AddFile("raw.bin", data);
    }
    ArchiveReader r(tmp.path);
    ASSERT_EQ(r.Open(), (kSuccess));
    std::vector<uint8_t> out;
    ASSERT_EQ(r.ExtractFile("raw.bin", out), (kSuccess));
    EXPECT_EQ(out, data);
}

// ---------------------------------------------------------------------------
// Soft delete — DeleteFile / ListDeletedFiles / RecoverFile
// ---------------------------------------------------------------------------
TEST(ArchiveTest, SoftDeleteHidesFile) {
    TempFile tmp;
    auto data = MakeData("to be deleted");

    {
        ArchiveWriter w(tmp.path);
        ASSERT_EQ(w.Init(), (kSuccess));
        (void)w.AddFile("gone.txt", data);
        (void)w.AddFile("kept.txt", MakeData("survivor"));
        EXPECT_EQ(w.DeleteFile("gone.txt"), (kSuccess));
    }

    ArchiveReader r(tmp.path);
    ASSERT_EQ(r.Open(), (kSuccess));

    // gone.txt must not appear in normal listing
    auto files = r.ListFiles();
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0], "kept.txt");

    // gone.txt must appear in deleted listing
    auto deleted = r.ListDeletedFiles();
    ASSERT_EQ(deleted.size(), 1u);
    EXPECT_EQ(deleted[0], "gone.txt");

    // ExtractFile on a deleted file must return kFileNotFound
    std::vector<uint8_t> dummy;
    EXPECT_EQ(r.ExtractFile("gone.txt", dummy), XSTD_returnError(kFileNotFound));
}

TEST(ArchiveTest, RecoverDeletedFile) {
    TempFile tmp;
    auto data = MakeData("recoverable content");

    {
        ArchiveWriter w(tmp.path);
        ASSERT_EQ(w.Init(), (kSuccess));
        (void)w.AddFile("recover_me.txt", data);
        EXPECT_EQ(w.DeleteFile("recover_me.txt"), (kSuccess));
    }

    ArchiveReader r(tmp.path);
    ASSERT_EQ(r.Open(), (kSuccess));

    auto recovered = r.RecoverFile("recover_me.txt");
    ASSERT_TRUE(recovered.has_value());
    EXPECT_EQ(*recovered, data);

    // RecoverFile on a non-deleted file returns nullopt
    EXPECT_FALSE(r.RecoverFile("nonexistent.txt").has_value());
}

TEST(ArchiveTest, DeleteNonexistentReturnsFalse) {
    TempFile tmp;
    {
        ArchiveWriter w(tmp.path);
        ASSERT_EQ(w.Init(), (kSuccess));
        (void)w.AddFile("a.txt", MakeData("hello"));
        EXPECT_EQ(w.DeleteFile("does_not_exist.txt"), XSTD_returnError(kFileNotFound));
    }
}

// ---------------------------------------------------------------------------
// Encrypted archive — footer encrypted round-trip
// ---------------------------------------------------------------------------
TEST(ArchiveTest, EncryptedFooterRoundTrip) {
    TempFile tmp;
    auto key  = AES256Key();
    auto data = MakeData("secret with encrypted footer");

    {
        ArchiveWriterOptions opts;
        opts.encryption = ArchiveEncryption::Make(EncryptionAlgorithm::AES_GCM_V1,
                                                  AesKeySize::AES_256);
        opts.key = key;
        ArchiveWriter w(tmp.path, opts);
        ASSERT_EQ(w.Init(), (kSuccess));
        (void)w.AddFile("encrypted.bin", data);
    }

    ArchiveReaderOptions ropts;
    ropts.key = key;
    ArchiveReader r(tmp.path, ropts);
    ASSERT_EQ(r.Open(), (kSuccess));
    EXPECT_EQ(r.FileCount(), 1u);
    std::vector<uint8_t> out;
    ASSERT_EQ(r.ExtractFile("encrypted.bin", out), (kSuccess));
    EXPECT_EQ(out, data);
}
