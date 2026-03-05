#include "commands/delete.h"
#include "util.h"

#include <filesystem>
#include <string>
#include <vector>

#include <fmt/core.h>

#include "archive.h"

namespace xstd::cli {

void RegisterDelete(CLI::App& app) {
    struct Opts {
        std::string archive;
        std::string target_path;
        std::string key_hex;
        std::string key_file;
        bool        soft{false};
    };
    auto opts = std::make_shared<Opts>();

    auto* sub = app.add_subcommand("delete",
        "Delete a file from an Xstd archive (use --soft to keep data recoverable)");

    sub->add_option("archive",      opts->archive,      "Target .xstd archive path")->required();
    sub->add_option("path",         opts->target_path,  "In-archive path to delete")->required();
    sub->add_option("--key",        opts->key_hex,      "Decryption/re-encryption key as hex");
    sub->add_option("--key-file",   opts->key_file,     "Path to binary key file");
    sub->add_flag("--soft",         opts->soft,         "Soft-delete: mark as deleted but keep data recoverable");

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

        VLog("Archive : {} ({} file(s))", opts->archive, arch.FileCount());
        VLog("Target  : {}", opts->target_path);

        if (auto res = arch.DeleteFile(opts->target_path, opts->soft); XSTD_isError(res)) {
            HandleResult(res, fmt::format("deleting '{}'", opts->target_path));
            std::exit(1);
        }

        if (auto res = arch.Close(); XSTD_isError(res)) {
            HandleResult(res, "closing archive");
            std::exit(1);
        }

        if (opts->soft) {
            fmt::print("Soft-deleted '{}' from '{}'.\n", opts->target_path, opts->archive);
            fmt::print("Use 'xli recover' to restore it.\n");
        } else {
            fmt::print("Deleted '{}' from '{}'.\n", opts->target_path, opts->archive);
        }
    });
}

} // namespace xstd::cli
