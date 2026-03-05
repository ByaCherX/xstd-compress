// ---------------------------------------------------------------------------
// encryption_test.cc — Tests for AES-GCM and AES-CTR encryption.
// ---------------------------------------------------------------------------
#include <gtest/gtest.h>
#include "encryption.h"

#include <array>
#include <string>
#include <vector>

using namespace xstd;

// 32-byte test key (AES-256).
static std::vector<uint8_t> TestKey256() {
    std::vector<uint8_t> k(32);
    for (uint8_t i = 0; i < 32; ++i) k[i] = i;
    return k;
}

// 16-byte test key (AES-128).
static std::vector<uint8_t> TestKey128() {
    std::vector<uint8_t> k(16);
    for (uint8_t i = 0; i < 16; ++i) k[i] = i + 64;
    return k;
}

static std::vector<uint8_t> MakeData(std::string_view s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

// ---------------------------------------------------------------------------
// AES-GCM
// ---------------------------------------------------------------------------
class AesGcmTest : public ::testing::Test {
protected:
    AesGcmEncryptor enc;
    std::vector<uint8_t> key = TestKey256();
};

TEST_F(AesGcmTest, EncryptDecryptRoundTrip) {
    auto plaintext  = MakeData("Hello, Xstd encryption!");
    auto ciphertext = enc.Encrypt(plaintext, key);
    auto recovered  = enc.Decrypt(ciphertext, key);
    EXPECT_EQ(recovered, plaintext);
}

TEST_F(AesGcmTest, EmptyPlaintext) {
    std::vector<uint8_t> plaintext;
    auto ct = enc.Encrypt(plaintext, key);
    auto pt = enc.Decrypt(ct, key);
    EXPECT_TRUE(pt.empty());
}

TEST_F(AesGcmTest, CiphertextLargerThanPlaintext) {
    auto pt = MakeData("secret");
    auto ct = enc.Encrypt(pt, key);
    // IV (12) + ciphertext (6) + tag (16) = 34 bytes
    EXPECT_EQ(ct.size(), kAesGcmIvSize + pt.size() + kAesGcmTagSize);
}

TEST_F(AesGcmTest, DifferentIvEachEncryption) {
    auto pt  = MakeData("same plaintext");
    auto ct1 = enc.Encrypt(pt, key);
    auto ct2 = enc.Encrypt(pt, key);
    // IVs should be randomly different.
    bool same_iv = std::equal(ct1.begin(), ct1.begin() + kAesGcmIvSize, ct2.begin());
    EXPECT_FALSE(same_iv);
}

TEST_F(AesGcmTest, TamperedCiphertextFailsAuthentication) {
    auto pt = MakeData("important data");
    auto ct = enc.Encrypt(pt, key);
    ct[kAesGcmIvSize]++;   // flip one bit in ciphertext
    EXPECT_THROW(enc.Decrypt(ct, key), std::runtime_error);
}

TEST_F(AesGcmTest, TamperedTagFailsAuthentication) {
    auto pt = MakeData("important data");
    auto ct = enc.Encrypt(pt, key);
    ct.back() ^= 0xFF;     // corrupt the last byte of the auth tag
    EXPECT_THROW(enc.Decrypt(ct, key), std::runtime_error);
}

TEST_F(AesGcmTest, AadIncludedInAuthentication) {
    auto pt  = MakeData("secret file content");
    auto aad = MakeData("filename.txt/v1");
    auto ct  = enc.Encrypt(pt, key, aad);

    // Decrypt with correct AAD: succeeds.
    auto recovered = enc.Decrypt(ct, key, aad);
    EXPECT_EQ(recovered, pt);

    // Decrypt with wrong AAD: fails.
    auto wrong_aad = MakeData("filename.txt/v2");
    EXPECT_THROW(enc.Decrypt(ct, key, wrong_aad), std::runtime_error);
}

TEST_F(AesGcmTest, Aes128Key) {
    auto key128 = TestKey128();
    auto pt = MakeData("AES-128 test");
    auto ct = enc.Encrypt(pt, key128);
    auto recovered = enc.Decrypt(ct, key128);
    EXPECT_EQ(recovered, pt);
}

TEST_F(AesGcmTest, LargeData) {
    std::vector<uint8_t> big(1 * 1024 * 1024, 0xCC);   // 1 MiB
    auto ct = enc.Encrypt(big, key);
    auto pt = enc.Decrypt(ct, key);
    EXPECT_EQ(pt, big);
}

// ---------------------------------------------------------------------------
// AES-CTR
// ---------------------------------------------------------------------------
class AesCtrTest : public ::testing::Test {
protected:
    AesCtrEncryptor enc;
    std::vector<uint8_t> key = TestKey256();
};

TEST_F(AesCtrTest, EncryptDecryptRoundTrip) {
    auto pt = MakeData("CTR mode plaintext");
    auto ct = enc.Encrypt(pt, key);
    auto recovered = enc.Decrypt(ct, key);
    EXPECT_EQ(recovered, pt);
}

TEST_F(AesCtrTest, CiphertextSizeIsIvPlusPlaintext) {
    auto pt = MakeData("abc");
    auto ct = enc.Encrypt(pt, key);
    EXPECT_EQ(ct.size(), kAesCtrIvSize + pt.size());
}

TEST_F(AesCtrTest, EmptyRoundTrip) {
    std::vector<uint8_t> pt;
    auto ct = enc.Encrypt(pt, key);
    auto back = enc.Decrypt(ct, key);
    EXPECT_TRUE(back.empty());
}

TEST_F(AesCtrTest, DifferentIvEachEncryption) {
    auto pt  = MakeData("x");
    auto ct1 = enc.Encrypt(pt, key);
    auto ct2 = enc.Encrypt(pt, key);
    bool same_iv = std::equal(ct1.begin(), ct1.begin() + kAesCtrIvSize, ct2.begin());
    EXPECT_FALSE(same_iv);
}

// ---------------------------------------------------------------------------
// EncryptorFactory
// ---------------------------------------------------------------------------
TEST(EncryptorFactoryTest, CreatesGcm) {
    auto e = EncryptorFactory::Create(EncryptionAlgorithm::AES_GCM_V1);
    EXPECT_EQ(e->Algorithm(), EncryptionAlgorithm::AES_GCM_V1);
}

TEST(EncryptorFactoryTest, CreatesCtr) {
    auto e = EncryptorFactory::Create(EncryptionAlgorithm::AES_CTR_V1);
    EXPECT_EQ(e->Algorithm(), EncryptionAlgorithm::AES_CTR_V1);
}
