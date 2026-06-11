#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

#include "types.h"
#include "xstd_errors.h"

namespace xstd::cli {

// ---------------------------------------------------------------------------
// Verbose flag — set by main(), read by commands
// ---------------------------------------------------------------------------
extern bool g_verbose;

// ---------------------------------------------------------------------------
// Verbose logging helper — prints only when g_verbose == true
// ---------------------------------------------------------------------------
template <typename... Args>
inline void VLog(fmt::format_string<Args...> fmt_str, Args&&... args) {
    if (g_verbose) {
        fmt::print("[verbose] ");
        fmt::print(fmt_str, std::forward<Args>(args)...);
        fmt::print("\n");
    }
}

// ---------------------------------------------------------------------------
// Key resolution
// ---------------------------------------------------------------------------

/// Parse a hex string (e.g. "deadbeef…") into raw bytes.
/// Throws std::invalid_argument if the string has an odd length or
/// contains non-hex characters.
std::vector<uint8_t> ParseHexKey(std::string_view hex);

/// Read raw bytes from a file path.
/// Throws std::runtime_error on I/O failure.
std::vector<uint8_t> ReadKeyFile(const std::filesystem::path& path);

/// Resolve encryption key from --key / --key-file options.
/// - If both are provided → throws std::invalid_argument.
/// - If neither is provided and require_key is true → throws std::invalid_argument.
/// - If neither is provided and require_key is false → returns empty vector.
std::vector<uint8_t> ResolveKey(const std::string& hex_opt,
                                const std::string& file_opt,
                                bool require_key = false);

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

/// Human-readable byte size: "4.50 KiB", "1.23 MiB", etc.
std::string FormatSize(int64_t bytes);

/// ISO-8601-ish timestamp: "2026-02-28 14:30:00" from Unix seconds.
std::string FormatTime(int64_t unix_seconds);

/// Hex-encode a byte array (e.g. SHA-256 digest).
std::string ToHex(const uint8_t* data, size_t len);

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

/// Print a human-readable error from an XSTD_Result to stderr and return 1.
/// Returns 0 if res is success.
int HandleResult(XSTD_Result res, std::string_view context);

/// Throws std::runtime_error with a formatted message if res is an error.
void ThrowOnResult(XSTD_Result res, std::string_view context);

// ---------------------------------------------------------------------------
// Unified Compression & Encryption Parsing Helpers
// ---------------------------------------------------------------------------
CompressionType ParseCompressionType(std::string_view comp_str);
EncryptionAlgorithm ParseEncryptionAlgorithm(std::string_view enc_str);
AesKeySize MapKeySize(int key_size_bits);

} // namespace xstd::cli
