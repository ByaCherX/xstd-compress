#pragma once

// ---------------------------------------------------------------------------
// sha256.h — SHA-256 file-integrity hashing via OpenSSL 3 EVP API.
//
// Algorithm choice — SHA2-256 vs SHA3-256:
//   SHA2-256 is selected because modern x86/x64 CPUs carry Intel SHA-NI
//   hardware extensions that accelerate SHA-2 natively, making it 2-4×
//   faster than SHA3-256 (Keccak), which has no equivalent HW acceleration.
//
// One-shot  : Sha256::Hash(data)       — uses EVP_Q_digest (single call)
// Streaming : Sha256 h; h.Update(...); h.Finalise()  — for large/chunked data
// ---------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <span>
#include <string>

#include <openssl/evp.h>
#include <openssl/sha.h>

#include "constants.h"

namespace xstd {

class Sha256 {
public:
    using Digest = std::array<uint8_t, kSha256Size>;

    /// Opens an OpenSSL SHA2-256 provider context for streaming use.
    Sha256();
    ~Sha256();

    Sha256(const Sha256&)            = delete;
    Sha256& operator=(const Sha256&) = delete;
    Sha256(Sha256&&)                 = delete;
    Sha256& operator=(Sha256&&)      = delete;

    /// Feed the next chunk of data into the running hash.
    void Update(const void* data, std::size_t size);
    void Update(std::span<const uint8_t> data) { Update(data.data(), data.size()); }

    /// Returns the final 32-byte digest and resets the context for reuse.
    [[nodiscard]] Digest Finalise();

    // -----------------------------------------------------------------------
    // One-shot helpers  (prefer these for in-memory buffers)
    //   Internally: EVP_Q_digest — the most direct OpenSSL 3 path,
    //   avoids context allocation overhead for single-use calls.
    // -----------------------------------------------------------------------
    [[nodiscard]] static Digest Hash(const void* data, std::size_t size);
    [[nodiscard]] static Digest Hash(std::span<const uint8_t> data) {
        return Hash(data.data(), data.size());
    }

    /// Returns a lowercase 64-character hex string of @p digest.
    [[nodiscard]] static std::string ToHex(const Digest& digest);

private:
    EVP_MD*     md_{nullptr};   // fetched SHA2-256 algorithm object
    EVP_MD_CTX* ctx_{nullptr};  // per-hash state
};

} // namespace xstd
