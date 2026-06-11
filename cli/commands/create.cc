#include "commands/create.h"
#include "util.h"

#include <filesystem>
#include <string>
#include <vector>

#include <fmt/core.h>

#include "archive.h"
#include "types.h"

namespace xstd::cli {

#define ENC_WARNING \
    "[WARNING]: Encryption not supported in this version, use at your own risk. \
Encrypted archives created with this version may not be compatible with future versions of xstd-compress."

void RegisterCreate(CLI::App& app) {
    struct Opts {
        std::string              archive;
        std::vector<std::string> files;
        std::string              compression_str = "zstd";
        int                      level           = 3;
        std::string              encrypt_str;
        int                      key_size_int    = 256;
        std::string              key_hex;
        std::string              key_file;
        std::string              archive_path_override;
    };
    auto opts = std::make_shared<Opts>();

    auto* sub = app.add_subcommand("create",
        "Create a new Xstd archive and add files to it\n\n" ENC_WARNING);

    sub->add_option("archive",          opts->archive,    "Output .xstd archive path")->required();
    sub->add_option("files",            opts->files,      "Files to add to the archive");
    sub->add_option("-c,--compression", opts->compression_str,
        "Compression algorithm: none|zstd  (default: zstd)");
    sub->add_option("-l,--level",       opts->level,
        "Compression level 1-9  (default: 3)")->check(CLI::Range(1, 9));
    sub->add_option("--encrypt",        opts->encrypt_str,
        "Encryption algorithm: aes-gcm|aes-ctr  (omit for no encryption)");
    sub->add_option("--key-size",       opts->key_size_int,
        "AES key size in bits: 128|192|256  (default: 256)")
        ->check(CLI::IsMember({128, 192, 256}));
    sub->add_option("--key",            opts->key_hex,    "Encryption key as hex string");
    sub->add_option("--key-file",       opts->key_file,   "Path to binary key file");
    sub->add_option("--archive-path",   opts->archive_path_override,
        "Override the in-archive path (only valid when adding exactly 1 file)");

    sub->callback([opts]() {
        // ---- Compression ----
        const CompressionType comp_type = ParseCompressionType(opts->compression_str);
        const auto comp_level = static_cast<CompressionLevel>(opts->level);

        // ---- Encryption ----
        ArchiveEncryption enc = ArchiveEncryption::MakeNone();
        std::vector<uint8_t> key_bytes;

        if (!opts->encrypt_str.empty()) {
            const EncryptionAlgorithm alg = ParseEncryptionAlgorithm(opts->encrypt_str);
            const AesKeySize ks = MapKeySize(opts->key_size_int);
            enc = ArchiveEncryption::Make(alg, ks);
            key_bytes = ResolveKey(opts->key_hex, opts->key_file, /*require_key=*/true);
        }

        // ---- Validate --archive-path with multiple files ----
        if (!opts->archive_path_override.empty() && opts->files.size() > 1) {
            throw std::runtime_error("--archive-path can only be used with a single input file.");
        }

        // ---- Build archive options ----
        ArchiveOptions aopts;
        aopts.page_size  = PageSize::PAGE_64K;
        aopts.codec      = CompressionCodec{comp_type, comp_level};
        aopts.encryption = enc;
        aopts.key        = key_bytes;

        VLog("Creating archive : {}", opts->archive);
        VLog("  Compression    : {} (level {})", opts->compression_str, opts->level);
        VLog("  Encryption     : {}",
             opts->encrypt_str.empty() ? "none" : opts->encrypt_str);
        VLog("  Files to add   : {}", opts->files.size());

        Archive arch(opts->archive, aopts);

        ThrowOnResult(arch.Create(), "initialising archive");

        for (const auto& f : opts->files) {
            const std::filesystem::path disk_path(f);
            const std::string arc_path = opts->archive_path_override.empty()
                ? disk_path.filename().string()
                : opts->archive_path_override;

            VLog("  {} → {}", f, arc_path);

            ThrowOnResult(arch.AddFile(arc_path, disk_path), fmt::format("adding '{}'", f));
        }

        ThrowOnResult(arch.Close(), "finalising archive");

        fmt::print("Created '{}' with {} file(s).\n", opts->archive, opts->files.size());
    });
}

} // namespace xstd::cli
