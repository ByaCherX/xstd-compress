#pragma once
#include <CLI/CLI.hpp>
#include <vector>
#include <filesystem>
#include <cstdint>

namespace fs = std::filesystem;

namespace xstd::cli {
void Compress(const std::vector<uint8_t>& input, std::vector<uint8_t>& output, int level);
void Decompress(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);
void PackDirectory(const fs::path& dir_path, std::vector<uint8_t>& buffer);
void UnpackDirectory(const std::vector<uint8_t>& buffer, const fs::path& output_dir);
void RegisterCompress(CLI::App& app);
} // namespace xstd::cli
