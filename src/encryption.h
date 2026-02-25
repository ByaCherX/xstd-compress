#pragma once

// ---------------------------------------------------------------------------
// encryption.h — AES-GCM and AES-CTR encryption via OpenSSL EVP API.
//
// IEncryptor interface contract:
//   • Encrypt(plaintext, key, iv_out) → ciphertext (+ appended auth tag for GCM)
//   • Decrypt(ciphertext, key, iv)    → plaintext
//   • GenerateIV()                    → random IV bytes
//
// The IV is always prepended to the returned ciphertext buffer so that the
// caller only needs to store one contiguous blob per page.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "constants.h"
#include "types.h"

namespace xstd {

// ---------------------------------------------------------------------------
// IEncryptor — pure interface
// ---------------------------------------------------------------------------
class IEncryptor {
public:
    virtual ~IEncryptor() = default;

    /// Encrypts @p plaintext using @p key.
    /// Returns: [IV][ciphertext][tag?] as a single buffer.
    /// @p key must be 16, 24, or 32 bytes (AES-128/192/256).
    virtual std::vector<uint8_t> Encrypt(
        std::span<const uint8_t> plaintext,
        std::span<const uint8_t> key,
        std::span<const uint8_t> aad = {}) const = 0;

    /// Decrypts @p ciphertext (which starts with IV).
    /// Returns the original plaintext.
    virtual std::vector<uint8_t> Decrypt(
        std::span<const uint8_t> ciphertext_with_iv,
        std::span<const uint8_t> key,
        std::span<const uint8_t> aad = {}) const = 0;

    /// Returns the IV size in bytes for this algorithm.
    [[nodiscard]] virtual uint32_t IvSize()  const noexcept = 0;

    /// Returns the auth tag size in bytes (0 for CTR).
    [[nodiscard]] virtual uint32_t TagSize() const noexcept = 0;

    [[nodiscard]] virtual EncryptionAlgorithm Algorithm() const noexcept = 0;
};

// ---------------------------------------------------------------------------
// AesGcmEncryptor — AES-GCM (authenticated encryption)
// Layout: [IV 12 bytes][ciphertext][tag 16 bytes]
// ---------------------------------------------------------------------------
class AesGcmEncryptor final : public IEncryptor {
public:
    [[nodiscard]] uint32_t IvSize()  const noexcept override { return kAesGcmIvSize;  }
    [[nodiscard]] uint32_t TagSize() const noexcept override { return kAesGcmTagSize; }
    [[nodiscard]] EncryptionAlgorithm Algorithm() const noexcept override {
        return EncryptionAlgorithm::AES_GCM_V1;
    }

    std::vector<uint8_t> Encrypt(std::span<const uint8_t> plaintext,
                                  std::span<const uint8_t> key,
                                  std::span<const uint8_t> aad = {}) const override;

    std::vector<uint8_t> Decrypt(std::span<const uint8_t> ciphertext_with_iv,
                                  std::span<const uint8_t> key,
                                  std::span<const uint8_t> aad = {}) const override;
};

// ---------------------------------------------------------------------------
// AesCtrEncryptor — AES-CTR (fast, no authentication tag)
// Layout: [IV 16 bytes][ciphertext]
// ---------------------------------------------------------------------------
class AesCtrEncryptor final : public IEncryptor {
public:
    [[nodiscard]] uint32_t IvSize()  const noexcept override { return kAesCtrIvSize; }
    [[nodiscard]] uint32_t TagSize() const noexcept override { return 0; }
    [[nodiscard]] EncryptionAlgorithm Algorithm() const noexcept override {
        return EncryptionAlgorithm::AES_CTR_V1;
    }

    std::vector<uint8_t> Encrypt(std::span<const uint8_t> plaintext,
                                  std::span<const uint8_t> key,
                                  std::span<const uint8_t> aad = {}) const override;

    std::vector<uint8_t> Decrypt(std::span<const uint8_t> ciphertext_with_iv,
                                  std::span<const uint8_t> key,
                                  std::span<const uint8_t> aad = {}) const override;
};

// ---------------------------------------------------------------------------
// EncryptorFactory
// ---------------------------------------------------------------------------
struct EncryptorFactory {
    [[nodiscard]] static std::unique_ptr<IEncryptor> Create(EncryptionAlgorithm alg);
};

} // namespace xstd
