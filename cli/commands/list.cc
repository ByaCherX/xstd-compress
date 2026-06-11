#include "commands/list.h"
#include "util.h"

#include <string>
#include <vector>

#include <fmt/core.h>

#include "archive.h"
#include "metadata.h"

namespace xstd::cli {

void RegisterList(CLI::App& app) {
    struct Opts {
        std::string archive;
        std::string prefix;
        bool        show_deleted = false;
        std::string key_hex;
        std::string key_file;
    };
    auto opts = std::make_shared<Opts>();

    auto* sub = app.add_subcommand("list",
        "List files stored in an Xstd archive");

    sub->add_option("archive",      opts->archive,      "Source .xstd archive path")->required();
    sub->add_option("--prefix",     opts->prefix,       "Filter results by in-archive path prefix");
    sub->add_flag("--deleted",      opts->show_deleted, "Show soft-deleted files instead of active ones");
    sub->add_option("--key",        opts->key_hex,      "Decryption key as hex string");
    sub->add_option("--key-file",   opts->key_file,     "Path to binary key file");

    sub->callback([opts]() {
        std::vector<uint8_t> key_bytes = ResolveKey(opts->key_hex, opts->key_file);

        ArchiveOptions aopts;
        aopts.key = key_bytes;

        Archive arch(opts->archive, aopts);
        ThrowOnResult(arch.Open(), "opening archive");

        std::vector<std::string> files =
            opts->show_deleted  ? arch.ListDeletedFiles()
            : opts->prefix.empty() ? arch.ListFiles()
                                   : arch.ListDirectory(opts->prefix);

        if (files.empty()) {
            fmt::print("(no files)\n");
            return;
        }

        if (g_verbose) {
            // Verbose header
            fmt::print("{:<50s}  {:>10s}  {:>20s}  {:>20s}  {}\n",
                       "NAME", "SIZE", "CREATED", "MODIFIED", "SHA-256");
            fmt::print("{}\n", std::string(140, '-'));

            for (const auto& f : files) {
                auto meta = arch.Stat(f);
                if (!meta) {
                    fmt::print("{}\n", f);
                    continue;
                }
                fmt::print("{:<50s}  {:>10s}  {:>20s}  {:>20s}  {}\n",
                           f,
                           FormatSize(meta->original_size),
                           FormatTime(meta->created_time),
                           FormatTime(meta->last_modified_time),
                           ToHex(meta->checksum.data(), meta->checksum.size()));
            }
        } else {
            // Normal header
            fmt::print("{:<50s}  {:>10s}  {:>20s}\n", "NAME", "SIZE", "MODIFIED");
            fmt::print("{}\n", std::string(85, '-'));

            for (const auto& f : files) {
                auto meta = arch.Stat(f);
                if (!meta) {
                    fmt::print("{}\n", f);
                    continue;
                }
                fmt::print("{:<50s}  {:>10s}  {:>20s}\n",
                           f,
                           FormatSize(meta->original_size),
                           FormatTime(meta->last_modified_time));
            }
        }

        (void)arch.Close();
        fmt::print("\n{} file(s){}.\n",
                   files.size(),
                   opts->show_deleted ? " (deleted)" : "");
    });
}

} // namespace xstd::cli
