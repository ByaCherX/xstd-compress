#pragma once

// ---------------------------------------------------------------------------
// compression.h — Compression codec abstraction + ZSTD implementation.
//
// Pipeline position:
//   raw page data → [Encoding] → [ICompressor::Compress] → [IEncryptor::Encrypt] → disk
//   disk → [IEncryptor::Decrypt] → [ICompressor::Decompress] → [Decode] → raw page data
//
// Adding a new codec: subclass ICompressor, override Compress/Decompress,
// then register it in CompressorFactory::Create().
// ---------------------------------------------------------------------------

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "types.h"

namespace xstd {

// ---------------------------------------------------------------------------
// ICompressor — pure interface
// ---------------------------------------------------------------------------
class ICompressor {
public:
    virtual ~ICompressor() = default;

    /// Compress @p input into @p output.  Output is resized to fit exactly.
    /// @throws std::runtime_error on codec failure.
    virtual void Compress(std::span<const uint8_t> input,
                          std::vector<uint8_t>&    output) const = 0;

    /// Decompress @p input (compressed bytes) into @p output.
    /// @p uncompressed_size hint allows single-pass allocation;
    ///    pass 0 if unknown (codec will manage internally).
    /// @throws std::runtime_error on corruption or truncated data.
    virtual void Decompress(std::span<const uint8_t> input,
                            std::vector<uint8_t>&    output,
                            int32_t                  uncompressed_size) const = 0;

    [[nodiscard]] virtual CompressionType Type() const noexcept = 0;
};

// ---------------------------------------------------------------------------
// NoopCompressor — pass-through (UNCOMPRESSED)
// ---------------------------------------------------------------------------
class NoopCompressor final : public ICompressor {
public:
    void Compress(std::span<const uint8_t> input,
                  std::vector<uint8_t>&    output) const override {
        output.assign(input.begin(), input.end());
    }

    void Decompress(std::span<const uint8_t> input,
                    std::vector<uint8_t>&    output,
                    int32_t /*hint*/) const override {
        output.assign(input.begin(), input.end());
    }

    [[nodiscard]] CompressionType Type() const noexcept override {
        return CompressionType::UNCOMPRESSED;
    }
};

// ---------------------------------------------------------------------------
// ZstdCompressor — ZSTD (via vcpkg zstd)
// ---------------------------------------------------------------------------
class ZstdCompressor final : public ICompressor {
public:
    explicit ZstdCompressor(CompressionLevel level = CompressionLevel::DEFAULT);

    void Compress(std::span<const uint8_t> input,
                  std::vector<uint8_t>&    output) const override;

    void Decompress(std::span<const uint8_t> input,
                    std::vector<uint8_t>&    output,
                    int32_t                  uncompressed_size) const override;

    [[nodiscard]] CompressionType Type() const noexcept override {
        return CompressionType::ZSTD;
    }

private:
    int level_;
    static int ZstdLevel(CompressionLevel lvl) noexcept;
};

// ---------------------------------------------------------------------------
// CompressorFactory
// ---------------------------------------------------------------------------
struct CompressorFactory {
    [[nodiscard]] static std::unique_ptr<ICompressor> Create(CompressionCodec codec);
};

} // namespace xstd
