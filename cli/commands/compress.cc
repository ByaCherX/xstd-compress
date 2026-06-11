#include "commands/compress.h"
#include "util.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>

#include <fmt/core.h>
#include "zstd.h"

namespace fs = std::filesystem;

namespace xstd::cli {

void Compress(const std::vector<uint8_t>& input, std::vector<uint8_t>& output, int level) {
    size_t compressed_bound = ZSTD_compressBound(input.size());
    output.resize(compressed_bound);

    size_t compressed_size = ZSTD_compress(
        output.data(), output.size(), 
        input.data(), input.size(), 
        level
    );
    if (ZSTD_isError(compressed_size)) {
        throw std::runtime_error(fmt::format("Compression error: {}", ZSTD_getErrorName(compressed_size)));
    }
    output.resize(compressed_size);
};

void Decompress(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    unsigned long long decompressed_size = ZSTD_getFrameContentSize(input.data(), input.size());
    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR) {
        throw std::runtime_error("Decompression error: not a valid compressed frame.");
    } else if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        throw std::runtime_error("Decompression error: original size unknown.");
    }

    output.resize(decompressed_size);
    size_t result = ZSTD_decompress(
        output.data(), output.size(), 
        input.data(), input.size()
    );
    if (ZSTD_isError(result)) {
        throw std::runtime_error(fmt::format("Decompression error: {}", ZSTD_getErrorName(result)));
    }
};

// Helper function to pack a directory into a binary buffer
void PackDirectory(const fs::path& dir_path, std::vector<uint8_t>& buffer) {
    for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
        // Use relative path to preserve directory structure recursively
        std::string name = fs::relative(entry.path(), dir_path).generic_string();
        uint32_t name_len = static_cast<uint32_t>(name.length());
        bool is_dir = entry.is_directory();
        uint64_t file_size = 0;

        if (entry.is_regular_file()) {
            std::ifstream file(entry.path(), std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                throw std::runtime_error(fmt::format("Error: failed to open file '{}'.", entry.path().string()));
            }
            file_size = static_cast<uint64_t>(file.tellg());
            file.seekg(0, std::ios::beg);

            std::size_t old_size = buffer.size();
            std::size_t header_size = sizeof(uint32_t) + name_len + sizeof(bool) + sizeof(uint64_t);
            buffer.resize(old_size + header_size + static_cast<std::size_t>(file_size));

            uint8_t* ptr = buffer.data() + old_size;
            std::memcpy(ptr, &name_len, sizeof(uint32_t)); ptr += sizeof(uint32_t);
            std::memcpy(ptr, name.data(), name_len); ptr += name_len;
            std::memcpy(ptr, &is_dir, sizeof(bool)); ptr += sizeof(bool);
            std::memcpy(ptr, &file_size, sizeof(uint64_t)); ptr += sizeof(uint64_t);

            if (file_size > 0) {
                file.read(reinterpret_cast<char*>(ptr), static_cast<std::streamsize>(file_size));
            }
        } else {
            std::size_t old_size = buffer.size();
            std::size_t header_size = sizeof(uint32_t) + name_len + sizeof(bool) + sizeof(uint64_t);
            buffer.resize(old_size + header_size);

            uint8_t* ptr = buffer.data() + old_size;
            std::memcpy(ptr, &name_len, sizeof(uint32_t)); ptr += sizeof(uint32_t);
            std::memcpy(ptr, name.data(), name_len); ptr += name_len;
            std::memcpy(ptr, &is_dir, sizeof(bool)); ptr += sizeof(bool);
            std::memcpy(ptr, &file_size, sizeof(uint64_t)); ptr += sizeof(uint64_t);
        }
    }
}

// Helper function to unpack a binary buffer into a directory
void UnpackDirectory(const std::vector<uint8_t>& buffer, const fs::path& output_dir) {
    if (!fs::exists(output_dir)) {
        fs::create_directories(output_dir);
    }

    size_t offset = 0;
    while (offset < buffer.size()) {
        if (offset + sizeof(uint32_t) > buffer.size()) break;

        // Read name length
        uint32_t name_len;
        std::memcpy(&name_len, buffer.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        if (offset + name_len > buffer.size()) break;

        // Read name
        std::string name(buffer.data() + offset, buffer.data() + offset + name_len);
        offset += name_len;

        if (offset + sizeof(bool) > buffer.size()) break;

        // Read is_dir flag
        bool is_dir;
        std::memcpy(&is_dir, buffer.data() + offset, sizeof(bool));
        offset += sizeof(bool);

        if (offset + sizeof(uint64_t) > buffer.size()) break;

        // Read file size
        uint64_t file_size;
        std::memcpy(&file_size, buffer.data() + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);

        fs::path item_path = output_dir / name;

        if (is_dir) {
            fs::create_directories(item_path);
        } else {
            fs::create_directories(item_path.parent_path());
            if (offset + file_size > buffer.size()) break;

            std::ofstream out_file(item_path, std::ios::binary);
            if (!out_file.is_open()) {
                throw std::runtime_error(fmt::format("Error: failed to create file '{}'.", item_path.string()));
            }
            if (file_size > 0) {
                out_file.write(reinterpret_cast<const char*>(buffer.data() + offset), file_size);
            }
            offset += file_size;
        }
    }
}

void RegisterCompress(CLI::App& app) {
    struct Opts {
        std::string input;
        std::string output;
        bool compress;
        bool decompress;
        int level = 3;
    };
    auto opts = std::make_shared<Opts>();

    auto* sub = app.add_subcommand("compress",
        "Compress or decompress files and directories");

    sub->add_option("path",             opts->input, "File or directory path to compress/decompress")->required();
    sub->add_option("-o,--output",      opts->output, "Output path (default: append '.out' or '.dir' to input path)");
    sub->add_flag("-c,--compress",      opts->compress, "Enable compression");
    sub->add_flag("-d,--decompress",    opts->decompress, "Enable decompression");
    sub->add_option("-l,--level",       opts->level,
        "Compression level  (default: 3)")->check(CLI::Range(ZSTD_minCLevel(), ZSTD_maxCLevel()));

    sub->callback([opts]() {
        fs::path input_path(opts->input);
        if (!fs::exists(input_path)) {
            throw std::runtime_error(fmt::format("Error: input path '{}' does not exist.", opts->input));
        }

        std::vector<uint8_t> data;
        bool is_directory = fs::is_directory(input_path);

        // Load data from file or directory
        if (is_directory) {
            PackDirectory(input_path, data);
        } else {
            std::ifstream f(input_path, std::ios::binary | std::ios::ate);
            if (!f.is_open()) {
                throw std::runtime_error(fmt::format("Error: failed to open input file '{}'.", opts->input));
            }
            std::streamsize size = f.tellg();
            f.seekg(0, std::ios::beg);
            data.resize(static_cast<std::size_t>(size));
            if (size > 0) {
                f.read(reinterpret_cast<char*>(data.data()), size);
            }
        }

        std::vector<uint8_t> result;
        if (opts->compress) {
            Compress(data, result, opts->level);
        } else if (opts->decompress) {
            Decompress(data, result);
        } else {
            throw std::runtime_error("Error: either --compress or --decompress must be specified.");
        }

        // Determine output path
        fs::path output_path;
        if (!opts->output.empty()) {
            output_path = fs::path(opts->output);
        } else {
            if (opts->compress) {
                output_path = input_path.string() + ".zst";
            } else {
                if (is_directory) {
                    output_path = input_path.string() + "_extracted";
                } else {
                    output_path = input_path.string() + ".out";
                }
            }
        }

        // Write output
        if (opts->decompress && is_directory) {
            // Decompressed output should be a directory
            UnpackDirectory(result, output_path);
            fmt::print("Successfully decompressed '{}' to directory '{}'.\n", 
                opts->input, output_path.string());
        } else if (opts->decompress) {
            // Decompressed output is a file
            std::ofstream out(output_path, std::ios::binary);
            if (!out.is_open()) {
                throw std::runtime_error(fmt::format("Error: failed to create decompressed file '{}'.", output_path.string()));
            }
            if (!result.empty()) {
                out.write(reinterpret_cast<const char*>(result.data()), result.size());
            }
            fmt::print("Successfully decompressed '{}' to '{}'.\n", 
                opts->input, output_path.string());
        } else {
            // Compressed output is always a file
            std::ofstream out(output_path, std::ios::binary);
            if (!out.is_open()) {
                throw std::runtime_error(fmt::format("Error: failed to create compressed file '{}'.", output_path.string()));
            }
            if (!result.empty()) {
                out.write(reinterpret_cast<const char*>(result.data()), result.size());
            }
            fmt::print("Successfully compressed '{}' to '{}'.\n", 
                opts->input, output_path.string());
        }
    });

    
}

} // namespace xstd::cli