#include "sha256.h"

#include <stdexcept>

namespace xstd {

// ---------------------------------------------------------------------------
// EVP_MD_fetch loads SHA2-256 through the OpenSSL 3 provider mechanism.
// This resolves the concrete implementation at runtime (default provider
// will pick up hardware acceleration — e.g. Intel SHA-NI — automatically).
// ---------------------------------------------------------------------------

Sha256::Sha256() {
    // Fetch the SHA2-256 algorithm object from the default provider.
    // "SHA2-256" is the canonical OpenSSL 3 name; "SHA256" is accepted too.
    md_ = EVP_MD_fetch(nullptr, "SHA2-256", nullptr);
    if (!md_) throw std::runtime_error("EVP_MD_fetch(SHA2-256) failed — check OpenSSL provider");

    ctx_ = EVP_MD_CTX_new();
    if (!ctx_) {
        EVP_MD_free(md_);
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }

    if (EVP_DigestInit_ex2(ctx_, md_, nullptr) != 1) {
        EVP_MD_CTX_free(ctx_);
        EVP_MD_free(md_);
        throw std::runtime_error("EVP_DigestInit_ex2(SHA2-256) failed");
    }
}

Sha256::~Sha256() {
    EVP_MD_CTX_free(ctx_);
    EVP_MD_free(md_);
}

// ---------------------------------------------------------------------------
// Streaming
// ---------------------------------------------------------------------------

void Sha256::Update(const void* data, std::size_t size) {
    if (EVP_DigestUpdate(ctx_, data, size) != 1)
        throw std::runtime_error("EVP_DigestUpdate(SHA2-256) failed");
}

Sha256::Digest Sha256::Finalise() {
    Digest d{};
    unsigned int len = 0;
    if (EVP_DigestFinal_ex(ctx_, d.data(), &len) != 1)
        throw std::runtime_error("EVP_DigestFinal_ex(SHA2-256) failed");

    // Reset immediately so the object can be reused without re-construction.
    if (EVP_DigestInit_ex2(ctx_, md_, nullptr) != 1)
        throw std::runtime_error("EVP_DigestInit_ex2(SHA2-256) reset failed");

    return d;
}

// ---------------------------------------------------------------------------
// One-shot  — EVP_Q_digest is the most direct OpenSSL 3 single-call path.
// No context object is allocated by the caller; OpenSSL manages it internally.
// ---------------------------------------------------------------------------

Sha256::Digest Sha256::Hash(const void* data, std::size_t size) {
    Digest d{};
    std::size_t len = 0;
    if (EVP_Q_digest(nullptr, "SHA2-256", nullptr, data, size, d.data(), &len) != 1)
        throw std::runtime_error("EVP_Q_digest(SHA2-256) failed");
    return d;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

std::string Sha256::ToHex(const Digest& d) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    s.reserve(kSha256Size * 2);
    for (const uint8_t byte : d) {
        s.push_back(kHex[byte >> 4]);
        s.push_back(kHex[byte & 0x0F]);
    }
    return s;
}

} // namespace xstd
