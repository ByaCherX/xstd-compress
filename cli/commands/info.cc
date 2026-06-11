#include "commands/info.h"
#include "util.h"

#include <string>

#include <fmt/core.h>

#include "archive.h"
#include "constants.h"
#include "metadata.h"
#include "types.h"

namespace xstd::cli {

namespace {

const char* EncryptionAlgorithmStr(EncryptionAlgorithm alg) {
    switch (alg) {
        case EncryptionAlgorithm::AES_GCM_V1: return "AES-GCM-V1";
        case EncryptionAlgorithm::AES_CTR_V1: return "AES-CTR-V1";
        case EncryptionAlgorithm::NONE:        return "none";
        default:                               return "unknown";
    }
}

const char* AesKeySizeStr(AesKeySize ks) {
    switch (ks) {
        case AesKeySize::AES_128: return "128-bit";
        case AesKeySize::AES_192: return "192-bit";
        case AesKeySize::AES_256: return "256-bit";
        default:                  return "unknown";
    }
}

} // anonymous namespace

void RegisterInfo(CLI::App& app) {
    struct Opts {
        std::string archive;
        std::string key_hex;
        std::string key_file;
    };
    auto opts = std::make_shared<Opts>();

    auto* sub = app.add_subcommand("info",
        "Display archive metadata and header information");

    sub->add_option("archive",      opts->archive,  "Source .xstd archive path")->required();
    sub->add_option("--key",        opts->key_hex,  "Decryption key as hex string");
    sub->add_option("--key-file",   opts->key_file, "Path to binary key file");

    sub->callback([opts]() {
        std::vector<uint8_t> key_bytes = ResolveKey(opts->key_hex, opts->key_file);

        ArchiveOptions aopts;
        aopts.key = key_bytes;

        Archive arch(opts->archive, aopts);
        ThrowOnResult(arch.Open(), "opening archive");

        const auto& hdr        = arch.Header();
        const auto  file_count = arch.FileCount();
        const auto  del_files  = arch.ListDeletedFiles();

        const auto  page_sz    = static_cast<PageSize>(hdr.page_size);
        const uint32_t pg_bytes = PageSizeBytes(page_sz);

        // ---- Normal output ----
        fmt::print("Archive          : {}\n", opts->archive);
        fmt::print("Format version   : {}\n", hdr.version);
        fmt::print("Page size        : {} ({} bytes)\n",
                   pg_bytes >= 1024 * 1024
                       ? fmt::format("{} MiB", pg_bytes / (1024 * 1024))
                       : fmt::format("{} KiB", pg_bytes / 1024),
                   pg_bytes);
        fmt::print("Files            : {} active, {} deleted\n",
                   file_count, del_files.size());
        fmt::print("Compression      : {}\n",
                   hdr.IsCompressed() ? "yes" : "no");
        fmt::print("Encryption       : {}\n",
                   hdr.IsEncrypted()
                       ? fmt::format("{} / {}",
                                     EncryptionAlgorithmStr(hdr.encryption.GetAlgorithm()),
                                     AesKeySizeStr(hdr.encryption.GetKeySize()))
                       : "none");

        if (g_verbose) {
            fmt::print("\n--- Verbose Header Details ---\n");
            fmt::print("Magic            : 0x{:08X}  (expected 0x{:08X})\n",
                       hdr.magic, kMagic);
            fmt::print("Min read version : {}\n", kMinReadVersion);
            fmt::print("Number of pages  : {}\n", hdr.num_pages);
            fmt::print("Partial metadata protection: {}\n",
                       hdr.HasPartialMetadataProtection() ? "yes" : "no");

            if (!del_files.empty()) {
                fmt::print("\nDeleted files:\n");
                for (const auto& f : del_files)
                    fmt::print("  [deleted] {}\n", f);
            }

            if (file_count > 0) {
                fmt::print("\nActive files:\n");
                for (const auto& f : arch.ListFiles()) {
                    auto meta = arch.Stat(f);
                    if (!meta) continue;
                    fmt::print("  {:50s}  {:>10s}  pages: {}\n",
                               f,
                               FormatSize(meta->original_size),
                               meta->pages.size());
                }
            }
        }

        (void)arch.Close();
    });
}

} // namespace xstd::cli
