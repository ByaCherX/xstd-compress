#include "util.h"

#include <charconv>
#include <chrono>
#include <ctime>
#include <fstream>
#include <ios>
#include <sstream>
#include <stdexcept>

#include <fmt/core.h>

namespace xstd::cli {

bool g_verbose = false;

// ---------------------------------------------------------------------------
// Key resolution
// ---------------------------------------------------------------------------

std::vector<uint8_t> ParseHexKey(std::string_view hex) {
    if (hex.size() % 2 != 0) {
        throw std::invalid_argument(
            fmt::format("Hex key has odd length ({}); expected an even number of hex characters.",
                        hex.size()));
    }

    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);

    for (size_t i = 0; i < hex.size(); i += 2) {
        uint8_t byte = 0;
        auto result = std::from_chars(hex.data() + i, hex.data() + i + 2, byte, 16);
        if (result.ec != std::errc{}) {
            throw std::invalid_argument(
                fmt::format("Invalid hex character at position {}: '{}'",
                            i, hex.substr(i, 2)));
        }
        out.push_back(byte);
    }

    return out;
}

std::vector<uint8_t> ReadKeyFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        throw std::runtime_error(
            fmt::format("Cannot open key file: {}", path.string()));
    }
    auto size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

std::vector<uint8_t> ResolveKey(const std::string& hex_opt,
                                const std::string& file_opt,
                                bool require_key) {
    const bool has_hex  = !hex_opt.empty();
    const bool has_file = !file_opt.empty();

    if (has_hex && has_file) {
        throw std::invalid_argument("--key and --key-file are mutually exclusive.");
    }

    if (has_hex)  return ParseHexKey(hex_opt);
    if (has_file) return ReadKeyFile(file_opt);

    if (require_key) {
        throw std::invalid_argument(
            "Encryption is enabled but no key was provided. "
            "Use --key <hex> or --key-file <path>.");
    }

    return {};
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

std::string FormatSize(int64_t bytes) {
    if (bytes < 0) return "?";
    const double d = static_cast<double>(bytes);

    if (bytes < 1024)
        return fmt::format("{} B", bytes);
    if (bytes < 1024 * 1024)
        return fmt::format("{:.2f} KiB", d / 1024.0);
    if (bytes < 1024 * 1024 * 1024)
        return fmt::format("{:.2f} MiB", d / (1024.0 * 1024.0));

    return fmt::format("{:.2f} GiB", d / (1024.0 * 1024.0 * 1024.0));
}

std::string FormatTime(int64_t unix_seconds) {
    if (unix_seconds <= 0) return "N/A";

    const std::time_t t = static_cast<std::time_t>(unix_seconds);
    std::tm tm_val{};

#if defined(_WIN32)
    gmtime_s(&tm_val, &t);
#else
    gmtime_r(&t, &tm_val);
#endif

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_val);
    return buf;
}

std::string ToHex(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += fmt::format("{:02x}", data[i]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

int HandleResult(XSTD_Result res, std::string_view context) {
    if (!XSTD_isError(res)) return 0;
    fmt::print(stderr, "Error {}: {}\n", context, XSTD_ErrorCode_toString(res));
    return 1;
}

} // namespace xstd::cli
