// ---------------------------------------------------------------------------
// compression_test.cc — Tests for ICompressor / ZstdCompressor.
// ---------------------------------------------------------------------------
#include <gtest/gtest.h>
#include "compression.h"

#include <numeric>
#include <string>
#include <vector>

using namespace xstd;

// ---------------------------------------------------------------------------
// NoopCompressor
// ---------------------------------------------------------------------------
TEST(NoopCompressorTest, RoundTrip) {
    NoopCompressor c;
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    std::vector<uint8_t> out;
    c.Compress(data, out);
    EXPECT_EQ(out, data);

    std::vector<uint8_t> back;
    c.Decompress(out, back, static_cast<int32_t>(data.size()));
    EXPECT_EQ(back, data);
}

TEST(NoopCompressorTest, EmptyInput) {
    NoopCompressor c;
    std::vector<uint8_t> out;
    c.Compress({}, out);
    EXPECT_TRUE(out.empty());
}

// ---------------------------------------------------------------------------
// ZstdCompressor
// ---------------------------------------------------------------------------
class ZstdCompressorTest : public ::testing::Test {
protected:
    ZstdCompressor compressor{CompressionCodec{CompressionType::ZSTD, CompressionLevel::XSTD_greedy}};
    ZstdCompressor fast_compressor{CompressionCodec{CompressionType::ZSTD, CompressionLevel::XSTD_fast}};
    ZstdCompressor best_compressor{CompressionCodec{CompressionType::ZSTD, CompressionLevel::XSTD_bmax}};
};

TEST_F(ZstdCompressorTest, SmallRoundTrip) {
    std::string text = "Hello, Xstd!";
    std::vector<uint8_t> input(text.begin(), text.end());
    std::vector<uint8_t> compressed, decompressed;

    compressor.Compress(input, compressed);
    EXPECT_GT(compressed.size(), 0u);

    compressor.Decompress(compressed, decompressed, static_cast<int32_t>(input.size()));
    EXPECT_EQ(decompressed, input);
}

TEST_F(ZstdCompressorTest, LargeRepetitiveDataCompresses) {
    // Highly compressible: 128 KiB of repeated bytes.
    std::vector<uint8_t> input(128 << 10, 0xAB);
    std::vector<uint8_t> compressed;
    compressor.Compress(input, compressed);
    std::cout << "Original size: " << input.size() << " bytes, "
              << "Compressed size: " << compressed.size() << " bytes\n";
    EXPECT_LT(compressed.size(), input.size() / 2);
}

TEST_F(ZstdCompressorTest, RandomDataRoundTrip) {
    std::vector<uint8_t> input(8 * 1024);
    std::iota(input.begin(), input.end(), 0u);
    std::vector<uint8_t> compressed, decompressed;

    compressor.Compress(input, compressed);
    compressor.Decompress(compressed, decompressed, static_cast<int32_t>(input.size()));
    EXPECT_EQ(decompressed, input);
}

TEST_F(ZstdCompressorTest, EmptyInputRoundTrip) {
    std::vector<uint8_t> compressed, decompressed;
    compressor.Compress({}, compressed);
    compressor.Decompress(compressed, decompressed, 0);
    EXPECT_TRUE(decompressed.empty());
}

TEST_F(ZstdCompressorTest, AllLevelsRoundTrip) {
    std::vector<uint8_t> input(1024, 42);
    for (auto* c : { &compressor, &fast_compressor, &best_compressor }) {
        std::vector<uint8_t> comp, decomp;
        c->Compress(input, comp);
        c->Decompress(comp, decomp, static_cast<int32_t>(input.size()));
        EXPECT_EQ(decomp, input);
    }
}

TEST_F(ZstdCompressorTest, DecompressWithoutSizeHint) {
    std::vector<uint8_t> input = {10, 20, 30, 40, 50};
    std::vector<uint8_t> compressed, decompressed;
    compressor.Compress(input, compressed);
    // Pass hint = 0 → uses ZSTD frame content size.
    compressor.Decompress(compressed, decompressed, 0);
    EXPECT_EQ(decompressed, input);
}

TEST_F(ZstdCompressorTest, CorruptDataThrows) {
    std::vector<uint8_t> garbage = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    std::vector<uint8_t> out;
    EXPECT_EQ(compressor.Decompress(garbage, out, 100), XSTD_returnError(kDecompressionFailed));
}

// ---------------------------------------------------------------------------
// CompressorFactory
// ---------------------------------------------------------------------------
TEST(CompressorFactoryTest, CreateUncompressed) {
    auto c = CompressorFactory::Create(CompressionCodec{CompressionType::UNCOMPRESSED, CompressionLevel::XSTD_greedy});
    EXPECT_EQ(c->Codec().Type(), CompressionType::UNCOMPRESSED);
}

TEST(CompressorFactoryTest, CreateZstd) {
    auto c = CompressorFactory::Create(CompressionCodec{CompressionType::ZSTD, CompressionLevel::XSTD_fast});
    EXPECT_EQ(c->Codec().Type(), CompressionType::ZSTD);
}
