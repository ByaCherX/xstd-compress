#include "commands/extract.h"
#include "util.h"

#include <filesystem>
#include <string>
#include <vector>

#include <fmt/core.h>

#include "archive.h"
#include "metadata.h"

namespace xstd::cli {

void RegisterExtract(CLI::App& app) {
    struct Opts {
        std::string              archive;
        std::vector<std::string> paths;
        std::string              output_dir;
        std::string              key_hex;
        std::string              key_file;
    };
    auto opts = std::make_shared<Opts>();

    auto* sub = app.add_subcommand("extract",
        "Extract files from an Xstd archive to disk");

    sub->add_option("archive",    opts->archive,    "Source .xstd archive path")->required();
    sub->add_option("paths",      opts->paths,
        "In-archive paths to extract (omit to extract all)");
    sub->add_option("-o,--output-dir", opts->output_dir,
        "Output directory (default: current directory)");
    sub->add_option("--key",      opts->key_hex,    "Decryption key as hex string");
    sub->add_option("--key-file", opts->key_file,   "Path to binary key file");

    sub->callback([opts]() {
        std::vector<uint8_t> key_bytes;
        try {
            key_bytes = ResolveKey(opts->key_hex, opts->key_file);
        } catch (const std::exception& e) {
            fmt::print(stderr, "Error: {}\n", e.what());
            std::exit(1);
        }

        ArchiveOptions aopts;
        aopts.key = key_bytes;

        Archive arch(opts->archive, aopts);
        if (auto res = arch.Open(); XSTD_isError(res)) {
            HandleResult(res, "opening archive");
            std::exit(1);
        }

        // Determine which paths to extract
        const std::vector<std::string> targets =
            opts->paths.empty() ? arch.ListFiles() : opts->paths;

        if (targets.empty()) {
            fmt::print("Archive '{}' contains no files.\n", opts->archive);
            return;
        }

        // Prepare output directory
        const std::filesystem::path out_dir =
            opts->output_dir.empty() ? std::filesystem::current_path()
                                     : std::filesystem::path(opts->output_dir);

        std::filesystem::create_directories(out_dir);
        VLog("Output directory : {}", out_dir.string());
        VLog("Files to extract : {}", targets.size());

        int extracted_count = 0;
        for (const auto& arc_path : targets) {
            const std::filesystem::path dest = out_dir / arc_path;

            // Create parent directories
            std::filesystem::create_directories(dest.parent_path());

            if (auto res = arch.ExtractFile(arc_path, dest); XSTD_isError(res)) {
                HandleResult(res, fmt::format("extracting '{}'", arc_path));
                std::exit(1);
            }

            if (g_verbose) {
                auto meta = arch.Stat(arc_path);
                if (meta) {
                    fmt::print("  {:50s}  {:>10s}  {}\n",
                               arc_path,
                               FormatSize(meta->original_size),
                               ToHex(meta->checksum.data(), meta->checksum.size()));
                } else {
                    fmt::print("  {}\n", arc_path);
                }
            } else {
                fmt::print("  {}\n", arc_path);
            }
            ++extracted_count;
        }

        (void)arch.Close();
        fmt::print("Extracted {} file(s) to '{}'.\n", extracted_count, out_dir.string());
    });
}

} // namespace xstd::cli
