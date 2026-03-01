#include "encryption.h"
#include "xstd_errors.h"

#include <array>
#include <stdexcept>
#include <string>

#include <openssl/evp.h>
#include <openssl/rand.h>

namespace xstd {
namespace {

// ---------------------------------------------------------------------------
// RAII wrapper for EVP_CIPHER_CTX
// ---------------------------------------------------------------------------
struct EvpCtxDeleter {
    void operator()(EVP_CIPHER_CTX* p) const noexcept { EVP_CIPHER_CTX_free(p); }
};
using EvpCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, EvpCtxDeleter>;

static EvpCtxPtr MakeCtx() {
    EvpCtxPtr ctx{EVP_CIPHER_CTX_new()};
    if (!ctx) XSTD_THROW_ERROR_MSG(kGENERIC, "EVP_CIPHER_CTX_new failed");
    return ctx;
}

// Returns the AES-GCM EVP_CIPHER for the given key size (bytes).
static const EVP_CIPHER* AesGcmCipher(std::size_t key_len) {
    switch (key_len) {
        case 16: return EVP_aes_128_gcm();
        case 24: return EVP_aes_192_gcm();
        case 32: return EVP_aes_256_gcm();
        default: XSTD_THROW_ERROR_MSG(kInvalidArgument, "Invalid AES key length for GCM (must be 16, 24, or 32 bytes)");
    }
}

// Returns the AES-CTR EVP_CIPHER for the given key size (bytes).
static const EVP_CIPHER* AesCtrCipher(std::size_t key_len) {
    switch (key_len) {
        case 16: return EVP_aes_128_ctr();
        case 24: return EVP_aes_192_ctr();
        case 32: return EVP_aes_256_ctr();
        default: XSTD_THROW_ERROR_MSG(kInvalidArgument, "Invalid AES key length for CTR (must be 16, 24, or 32 bytes)");
    }
}

// Fills @p buf with @p len cryptographically-random bytes via OpenSSL RAND.
static void RandomBytes(uint8_t* buf, int len) {
    if (RAND_bytes(buf, len) != 1)
        XSTD_THROW_ERROR_MSG(kIOError, "RAND_bytes failed — OpenSSL PRNG failure");
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// AesGcmEncryptor
// ---------------------------------------------------------------------------

std::vector<uint8_t> AesGcmEncryptor::Encrypt(
    std::span<const uint8_t> plaintext,
    std::span<const uint8_t> key,
    std::span<const uint8_t> aad) const
{
    // Generate random IV.
    std::array<uint8_t, kAesGcmIvSize> iv{};
    RandomBytes(iv.data(), static_cast<int>(iv.size()));

    auto ctx = MakeCtx();
    const EVP_CIPHER* cipher = AesGcmCipher(key.size());

    if (EVP_EncryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr) != 1)
        XSTD_THROW_ERROR_MSG(kGENERIC, "AES-GCM EncryptInit (cipher) failed");
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN,
                             static_cast<int>(iv.size()), nullptr) != 1)
        XSTD_THROW_ERROR_MSG(kGENERIC, "AES-GCM set IV len failed");
    if (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), iv.data()) != 1)
        XSTD_THROW_ERROR_MSG(kGENERIC, "AES-GCM EncryptInit (key+iv) failed");

    // Feed AAD if present.
    if (!aad.empty()) {
        int dummy = 0;
        if (EVP_EncryptUpdate(ctx.get(), nullptr, &dummy,
                              aad.data(), static_cast<int>(aad.size())) != 1)
            XSTD_THROW_ERROR_MSG(kGENERIC, "AES-GCM AAD update failed");
    }

    // Encrypt plaintext. Output = [IV][ciphertext][tag].
    std::vector<uint8_t> out;
    out.reserve(kAesGcmIvSize + plaintext.size() + kAesGcmTagSize);
    out.insert(out.end(), iv.begin(), iv.end());

    out.resize(kAesGcmIvSize + plaintext.size());
    int ct_len = 0;
    if (EVP_EncryptUpdate(ctx.get(),
                          out.data() + kAesGcmIvSize, &ct_len,
                          plaintext.data(), static_cast<int>(plaintext.size())) != 1)
        XSTD_THROW_ERROR_MSG(kGENERIC, "AES-GCM EncryptUpdate failed");

    int final_len = 0;
    if (EVP_EncryptFinal_ex(ctx.get(),
                             out.data() + kAesGcmIvSize + ct_len, &final_len) != 1)
        XSTD_THROW_ERROR_MSG(kGENERIC, "AES-GCM EncryptFinal failed");
    out.resize(kAesGcmIvSize + static_cast<std::size_t>(ct_len + final_len));

    // Append auth tag.
    std::array<uint8_t, kAesGcmTagSize> tag{};
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG,
                             static_cast<int>(tag.size()), tag.data()) != 1)
        XSTD_THROW_ERROR_MSG(kGENERIC, "AES-GCM get tag failed");
    out.insert(out.end(), tag.begin(), tag.end());
    return out;
}

std::vector<uint8_t> AesGcmEncryptor::Decrypt(
    std::span<const uint8_t> ciphertext_with_iv,
    std::span<const uint8_t> key,
    std::span<const uint8_t> aad) const
{
    const std::size_t min_size = kAesGcmIvSize + kAesGcmTagSize;
    if (ciphertext_with_iv.size() < min_size)
        XSTD_THROW_ERROR_MSG(kInvalidArgument, "AES-GCM ciphertext too short");

    const uint8_t* iv  = ciphertext_with_iv.data();
    const std::size_t ct_len = ciphertext_with_iv.size() - kAesGcmIvSize - kAesGcmTagSize;
    const uint8_t* ct  = iv + kAesGcmIvSize;
    const uint8_t* tag = ct + ct_len;

    auto ctx = MakeCtx();
    const EVP_CIPHER* cipher = AesGcmCipher(key.size());

    if (EVP_DecryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr) != 1)
        XSTD_THROW_ERROR_MSG(kGENERIC, "AES-GCM DecryptInit (cipher) failed");
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN,
                             static_cast<int>(kAesGcmIvSize), nullptr) != 1)
        XSTD_THROW_ERROR_MSG(kGENERIC, "AES-GCM set IV len failed");
    if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), iv) != 1)
        XSTD_THROW_ERROR_MSG(kGENERIC, "AES-GCM DecryptInit (key+iv) failed");

    if (!aad.empty()) {
        int dummy = 0;
        if (EVP_DecryptUpdate(ctx.get(), nullptr, &dummy,
                              aad.data(), static_cast<int>(aad.size())) != 1)
            XSTD_THROW_ERROR_MSG(kGENERIC, "AES-GCM AAD update failed");
    }

    std::vector<uint8_t> plaintext(ct_len);
    int pt_len = 0;
    if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &pt_len,
                          ct, static_cast<int>(ct_len)) != 1)
        XSTD_THROW_ERROR_MSG(kGENERIC, "AES-GCM DecryptUpdate failed");

    // Set expected tag.
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG,
                             static_cast<int>(kAesGcmTagSize),
                             const_cast<uint8_t*>(tag)) != 1)
        XSTD_THROW_ERROR_MSG(kGENERIC, "AES-GCM set tag failed");

    int final_len = 0;
    if (EVP_DecryptFinal_ex(ctx.get(),
                             plaintext.data() + pt_len, &final_len) != 1)
        XSTD_THROW_ERROR_MSG(kDecryptionFailed, "AES-GCM authentication tag mismatch — data corrupted or wrong key");

    plaintext.resize(static_cast<std::size_t>(pt_len + final_len));
    return plaintext;
}

// ---------------------------------------------------------------------------
// AesCtrEncryptor
// ---------------------------------------------------------------------------

std::vector<uint8_t> AesCtrEncryptor::Encrypt(
    std::span<const uint8_t> plaintext,
    std::span<const uint8_t> key,
    std::span<const uint8_t> /*aad*/) const
{
    std::array<uint8_t, kAesCtrIvSize> iv{};
    RandomBytes(iv.data(), static_cast<int>(iv.size()));

    auto ctx = MakeCtx();
    const EVP_CIPHER* cipher = AesCtrCipher(key.size());

    if (EVP_EncryptInit_ex(ctx.get(), cipher, nullptr, key.data(), iv.data()) != 1)
        XSTD_THROW_ERROR_MSG(kGENERIC, "AES-CTR EncryptInit failed");

    std::vector<uint8_t> out;
    out.reserve(kAesCtrIvSize + plaintext.size());
    out.insert(out.end(), iv.begin(), iv.end());
    out.resize(kAesCtrIvSize + plaintext.size());

    int n1 = 0, n2 = 0;
    if (EVP_EncryptUpdate(ctx.get(), out.data() + kAesCtrIvSize, &n1,
                          plaintext.data(), static_cast<int>(plaintext.size())) != 1)
        XSTD_THROW_ERROR_MSG(kGENERIC, "AES-CTR EncryptUpdate failed");
    if (EVP_EncryptFinal_ex(ctx.get(), out.data() + kAesCtrIvSize + n1, &n2) != 1)
        XSTD_THROW_ERROR_MSG(kGENERIC, "AES-CTR EncryptFinal failed");
    out.resize(kAesCtrIvSize + static_cast<std::size_t>(n1 + n2));
    return out;
}

std::vector<uint8_t> AesCtrEncryptor::Decrypt(
    std::span<const uint8_t> ciphertext_with_iv,
    std::span<const uint8_t> key,
    std::span<const uint8_t> /*aad*/) const
{
    if (ciphertext_with_iv.size() < kAesCtrIvSize)
        XSTD_THROW_ERROR_MSG(kInvalidArgument, "AES-CTR ciphertext too short");

    const uint8_t* iv  = ciphertext_with_iv.data();
    const std::size_t ct_len = ciphertext_with_iv.size() - kAesCtrIvSize;
    const uint8_t* ct  = iv + kAesCtrIvSize;

    auto ctx = MakeCtx();
    const EVP_CIPHER* cipher = AesCtrCipher(key.size());

    if (EVP_DecryptInit_ex(ctx.get(), cipher, nullptr, key.data(), iv) != 1)
        XSTD_THROW_ERROR_MSG(kGENERIC, "AES-CTR DecryptInit failed");

    std::vector<uint8_t> plaintext(ct_len);
    int n1 = 0, n2 = 0;
    if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &n1,
                          ct, static_cast<int>(ct_len)) != 1)
        XSTD_THROW_ERROR_MSG(kGENERIC, "AES-CTR DecryptUpdate failed");
    if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + n1, &n2) != 1)
        XSTD_THROW_ERROR_MSG(kGENERIC, "AES-CTR DecryptFinal failed");
    plaintext.resize(static_cast<std::size_t>(n1 + n2));
    return plaintext;
}

// ---------------------------------------------------------------------------
// EncryptorFactory
// ---------------------------------------------------------------------------

std::unique_ptr<IEncryptor> EncryptorFactory::Create(EncryptionAlgorithm alg) {
    switch (alg) {
        case EncryptionAlgorithm::AES_GCM_V1:
            return std::make_unique<AesGcmEncryptor>();
        case EncryptionAlgorithm::AES_CTR_V1:
            return std::make_unique<AesCtrEncryptor>();
        default:
            return nullptr;
    }
}

} // namespace xstd
