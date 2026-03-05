#include "commands/add.h"
#include "util.h"

#include <filesystem>
#include <string>
#include <vector>

#include <fmt/core.h>

#include "archive.h"
#include "types.h"

namespace xstd::cli {

void RegisterAdd(CLI::App& app) {
    struct Opts {
        std::string archive;
        std::string file;
        std::string archive_path_override;
        std::string compression_str = "zstd";
        int         level           = 3;
        std::string key_hex;
        std::string key_file;
    };
    auto opts = std::make_shared<Opts>();

    auto* sub = app.add_subcommand("add",
        "Add a file to an existing Xstd archive");

    sub->add_option("archive",          opts->archive,  "Target .xstd archive path")->required();
    sub->add_option("file",             opts->file,     "Disk path of the file to add")->required();
    sub->add_option("--archive-path",   opts->archive_path_override,
        "Override the in-archive path (default: filename)");
    sub->add_option("-c,--compression", opts->compression_str,
        "Compression for rewritten archive: none|zstd  (default: zstd)");
    sub->add_option("-l,--level",       opts->level,
        "Compression level 1-9  (default: 3)")->check(CLI::Range(1, 9));
    sub->add_option("--key",            opts->key_hex,  "Decryption/re-encryption key as hex");
    sub->add_option("--key-file",       opts->key_file, "Path to binary key file");

    sub->callback([opts]() {
        std::vector<uint8_t> key_bytes;
        try {
            key_bytes = ResolveKey(opts->key_hex, opts->key_file);
        } catch (const std::exception& e) {
            fmt::print(stderr, "Error: {}\n", e.what());
            std::exit(1);
        }

        // ---- Resolve compression ----
        CompressionType comp_type;
        if (opts->compression_str == "none") {
            comp_type = CompressionType::UNCOMPRESSED;
        } else if (opts->compression_str == "zstd") {
            comp_type = CompressionType::ZSTD;
        } else {
            fmt::print(stderr, "Error: unknown compression '{}'.  Use 'none' or 'zstd'.\n",
                       opts->compression_str);
            std::exit(1);
        }

        // ---- Open archive and add file via high-level API ----
        ArchiveOptions aopts;
        aopts.key   = key_bytes;
        aopts.codec = CompressionCodec{comp_type,
                                       static_cast<CompressionLevel>(opts->level)};

        Archive arch(opts->archive, aopts);
        if (auto res = arch.Open(); XSTD_isError(res)) {
            HandleResult(res, "opening archive");
            std::exit(1);
        }

        VLog("Archive: {} ({} file(s))", opts->archive, arch.FileCount());

        const std::filesystem::path disk_path(opts->file);
        const std::string arc_path = opts->archive_path_override.empty()
            ? disk_path.filename().string()
            : opts->archive_path_override;

        VLog("  Adding: {} \u2192 {}", opts->file, arc_path);

        if (auto res = arch.AddFile(arc_path, disk_path); XSTD_isError(res)) {
            HandleResult(res, fmt::format("adding '{}'", opts->file));
            std::exit(1);
        }

        if (auto res = arch.Close(); XSTD_isError(res)) {
            HandleResult(res, "closing archive");
            std::exit(1);
        }

        fmt::print("Added '{}' as '{}' in '{}'.\n", opts->file, arc_path, opts->archive);
    });
}

} // namespace xstd::cli
