#include "compression.h"
#include "xstd_errors.h"

#include <stdexcept>
#include <string>

#include <zstd.h>

namespace xstd {

// ---------------------------------------------------------------------------
// ZstdCompressor
// ---------------------------------------------------------------------------

static int ToZstdLevel(CompressionLevel lvl) noexcept {
    switch (lvl) {
        case CompressionLevel::XSTD_RESERVED_LEVEL:    return ZSTD_defaultCLevel();
        case CompressionLevel::XSTD_fast:              return 1;
        case CompressionLevel::XSTD_dfast:             return 3;
        case CompressionLevel::XSTD_greedy:            return 6;
        case CompressionLevel::XSTD_lazy:              return 9;
        case CompressionLevel::XSTD_lazy2:             return 12;
        case CompressionLevel::XSTD_btlazy:            return 15;
        case CompressionLevel::XSTD_btopt:             return 18;
        case CompressionLevel::XSTD_bultra:            return 20;
        case CompressionLevel::XSTD_bmax:              return ZSTD_maxCLevel();
        case CompressionLevel::XSTD_RESERVED_LEVEL_15: return ZSTD_defaultCLevel();
        default:                                       return ZSTD_defaultCLevel();
    }
}

ZstdCompressor::ZstdCompressor(CompressionCodec codec)
    : level_(ToZstdLevel(codec_.Level())) {
        this->codec_ = codec;
    }

int ZstdCompressor::ZstdLevel(CompressionLevel lvl) noexcept {
    return ToZstdLevel(lvl);
}

XSTD_Result ZstdCompressor::Compress(std::span<const uint8_t> input,
                                     std::vector<uint8_t>&    output) const {
    const std::size_t bound = ZSTD_compressBound(input.size());
    output.resize(bound);

    const std::size_t result = ZSTD_compress(
        output.data(), output.size(),
        input.data(), input.size(),
        level_);

    if (ZSTD_isError(result))
        #ifdef XSTD_ENABLE_DIRECT_THROW
        XSTD_THROW_ERROR_MSG(kCompressionFailed,
            std::string("ZSTD compress error: ") + ZSTD_getErrorName(result));
        #endif
        return XSTD_returnError(kCompressionFailed);

    output.resize(result);
    return XSTD_returnSuccess();
}

XSTD_Result ZstdCompressor::Decompress(std::span<const uint8_t> input,
                                       std::vector<uint8_t>&    output,
                                       int32_t                  uncompressed_size) const {
    if (uncompressed_size > 0) {
        output.resize(static_cast<std::size_t>(uncompressed_size));
        const std::size_t result = ZSTD_decompress(
            output.data(), output.size(),
            input.data(), input.size());
        if (ZSTD_isError(result))
            #ifdef XSTD_ENABLE_DIRECT_THROW
            XSTD_THROW_ERROR_MSG(kDecompressionFailed,
                std::string("ZSTD decompress error: ") + ZSTD_getErrorName(result));
            #endif
            return XSTD_returnError(kDecompressionFailed);
        output.resize(result); // Really unnecessary if uncompressed_size is correct, but be safe.
        return XSTD_returnSuccess();
    } else {
        // Unknown size — use streaming decompress.
        ZSTD_DStream* stream = ZSTD_createDStream();
        if (!stream) XSTD_returnError(kDecompressionFailed);

        ZSTD_initDStream(stream);
        output.clear();

        ZSTD_inBuffer  in_buf  {input.data(), input.size(), 0};
        std::vector<uint8_t> chunk(ZSTD_DStreamOutSize());
        ZSTD_outBuffer out_buf {chunk.data(), chunk.size(), 0};

        std::size_t ret = 0;
        do {
            out_buf.pos = 0;
            ret = ZSTD_decompressStream(stream, &out_buf, &in_buf);
            if (ZSTD_isError(ret)) {
                ZSTD_freeDStream(stream);
                #ifdef XSTD_ENABLE_DIRECT_THROW
                XSTD_THROW_ERROR_MSG(kGENERIC,
                    std::string("ZSTD decompress error: ") + ZSTD_getErrorName(ret));
                #endif
                return XSTD_returnError(kDecompressionFailed);
            }
            output.insert(output.end(), chunk.begin(), chunk.begin() + out_buf.pos);
        } while (ret != 0 && in_buf.pos < in_buf.size);

        ZSTD_freeDStream(stream);
        return XSTD_returnSuccess();
    }
}

// ---------------------------------------------------------------------------
// CompressorFactory
// ---------------------------------------------------------------------------

std::unique_ptr<ICompressor> CompressorFactory::Create(CompressionCodec codec) noexcept {
    switch (codec.Type()) {
        case CompressionType::UNCOMPRESSED:
            return std::make_unique<NoopCompressor>();
        case CompressionType::ZSTD:
            return std::make_unique<ZstdCompressor>(codec);
        default:
            return std::make_unique<ZstdCompressor>(codec);
    }
}

} // namespace xstd
