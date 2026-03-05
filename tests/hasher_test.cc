#include <gtest/gtest.h>
#include "xxhasher.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <numeric>
#include <vector>

namespace xstd {

TEST(XXHasherTest, EmptyDataReturnsZero) {
    EXPECT_EQ(XXHasher::Hash(nullptr, 0), 0u);
    const char data[] = "";
    EXPECT_EQ(XXHasher::Hash(data, 0), 0u);
}

TEST(XXHasherTest, ConsistentHash) {
    const char data[] = "Hello, Xstd!";
    const std::size_t len = std::strlen(data);
    EXPECT_EQ(XXHasher::Hash(data, len), XXHasher::Hash(data, len));
}

TEST(XXHasherTest, DifferentDataDifferentHash) {
    const char a[] = "Hello";
    const char b[] = "World";
    EXPECT_NE(XXHasher::Hash(a, std::strlen(a)), XXHasher::Hash(b, std::strlen(b)));
}

TEST(XXHasherTest, XstdPageSizeTest) {
    std::vector<uint8_t> data(64 << 10);
    std::iota(data.begin(), data.end(), uint8_t{0});
    const uint64_t h1 = XXHasher::Hash(data.data(), data.size());
    const uint64_t h2 = XXHasher::Hash(data.data(), data.size());
    EXPECT_EQ(h1, h2);
    EXPECT_NE(h1, 0u);
}

TEST(XXHasherTest, LargeBlock) {
    std::vector<uint8_t> data(64 << 20);
    std::iota(data.begin(), data.end(), uint8_t{0});
    
    auto start = std::chrono::high_resolution_clock::now();
    const uint64_t h1 = XXHasher::Hash(data.data(), data.size());
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "LargeBlock test completed in " << duration.count() << " ms" << std::endl;
    
    EXPECT_NE(h1, 0u);
}



TEST(XXHasherTest, PageSizedBlocks) {
    for (uint32_t sz : {4096u, 16384u, 65536u, 4194304u}) {
        std::vector<uint8_t> data(sz, 0xAB);
        EXPECT_NE(XXHasher::Hash(data.data(), data.size()), 0u) << "page size=" << sz;
    }
}

} // namespace xstd

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
