#include "commands/recover.h"
#include "util.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <fmt/core.h>

#include "archive.h"
#include "metadata.h"

namespace xstd::cli {

void RegisterRecover(CLI::App& app) {
    struct Opts {
        std::string archive;
        std::string target_path;
        std::string output_dir;
        std::string key_hex;
        std::string key_file;
    };
    auto opts = std::make_shared<Opts>();

    auto* sub = app.add_subcommand("recover",
        "Recover a soft-deleted file from an Xstd archive");

    sub->add_option("archive",          opts->archive,     "Source .xstd archive path")->required();
    sub->add_option("path",             opts->target_path, "In-archive path to recover")->required();
    sub->add_option("-o,--output-dir",  opts->output_dir,
        "Output directory (default: current directory)");
    sub->add_option("--key",            opts->key_hex,     "Decryption key as hex string");
    sub->add_option("--key-file",       opts->key_file,    "Path to binary key file");

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

        VLog("Recovering '{}' from '{}'", opts->target_path, opts->archive);

        auto recovered = arch.RecoverFile(opts->target_path);
        if (!recovered) {
            fmt::print(stderr,
                       "Error: '{}' could not be recovered (not found or not deleted).\n",
                       opts->target_path);
            std::exit(1);
        }

        (void)arch.Close();

        // ---- Write recovered data to disk ----
        const std::filesystem::path out_dir =
            opts->output_dir.empty() ? std::filesystem::current_path()
                                     : std::filesystem::path(opts->output_dir);

        const std::filesystem::path dest = out_dir / opts->target_path;
        std::filesystem::create_directories(dest.parent_path());

        std::ofstream out_file(dest, std::ios::binary);
        if (!out_file) {
            fmt::print(stderr, "Error: cannot write to '{}'\n", dest.string());
            std::exit(1);
        }
        out_file.write(reinterpret_cast<const char*>(recovered->data()),
                       static_cast<std::streamsize>(recovered->size()));
        out_file.close();

        if (g_verbose) {
            fmt::print("  Bytes recovered : {}\n", FormatSize(static_cast<int64_t>(recovered->size())));
            fmt::print("  Written to      : {}\n", dest.string());
        }

        fmt::print("Recovered '{}' → '{}'.\n", opts->target_path, dest.string());
    });
}

} // namespace xstd::cli
