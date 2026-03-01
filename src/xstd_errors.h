#pragma once

#ifndef XSTD_ERRORS_H
#define XSTD_ERRORS_H

#include <cstddef>
#include <stdexcept>

namespace xstd {
    
// Disable assertions in this header, as it's a low-level component.
#define XSTD_assert(expression) ((void)0) 

typedef enum {
    kSuccess = 0,
    kGENERIC  = 1,

    /* Input configuration */
    kInvalidArgument, // Bad options (e.g. key length mismatch).

    /* Object state */
    kAlreadyFinalised, // Write/delete attempted after Finalise().

    /* File-system */
    kCannotOpenFile, // Archive or source file could not be opened.
    kCannotWriteFile, // Destination file could not be written.

    /* Archive content */
    kFileNotFound, // Requested path not present in the catalog.
    kInvalidArchive, // Bad magic, truncated structure, or catalog error.

    /* Encryption */
    kDecryptionFailed, // Wrong key or authentication tag mismatch.
    kUnsupportedAlgorithm, // Unrecognized encryption or compression codec.

    /* Data integrity */
    kChecksumMismatch, // CRC-32 or SHA-256 verification failed.

    /* Generic */
    kIOError, // Unclassified I/O failure.
} XSTD_ErrorCode;

/** 
 * All XSTD functions return an XSTD_Result, which is a size_t where 0 represents success, 
 * and any other value represents an error code defined by the XSTD_ErrorCode enum.
 */
typedef size_t XSTD_Result;

/**
 * XSTD_Result is a size_t, where the value 0 represents success, and any other 
 * value represents an error code. The error code is defined by the XSTD_ErrorCode enum.
 */
inline int XSTD_isError(XSTD_Result res)
{
    return res != XSTD_ErrorCode::kSuccess;
}

/*-*********************************************
 * XSTD_Report helper functions
 *-*********************************************
 * These functions help create XSTD_Result values for success and error cases.
 */

inline XSTD_Result XSTD_returnSuccess(void)
{
    return (XSTD_Result)XSTD_ErrorCode::kSuccess;  
}

inline XSTD_Result XSTD_returnError(XSTD_ErrorCode errorCode)
{
    XSTD_assert(errorCode != XSTD_ErrorCode::kSuccess);
    return (XSTD_Result)errorCode;
}

/*-*********************************************
 *  XSTD_ErrorCode to string conversion
 *-*********************************************
 * Returns a human-readable string for the given XSTD_Result value.
 */
inline const char* XSTD_ErrorCode_toString(XSTD_Result res)
{
    switch (static_cast<XSTD_ErrorCode>(res)) {
        case kSuccess:             
            return "Success";
        case kGENERIC:              
            return "GENERIC";
        /* Compression */
        case kCompressionFailed:
            return "CompressionFailed: Compressor (e.g. ZSTD) failed to compress a page.";
        case kDecompressionFailed:
            return "DecompressionFailed: Decompressor (e.g. ZSTD) failed; data may be corrupt or truncated.";
            return "UnknownError";
    }
}

/*-*********************************************
 *  Error-handling macros
 *-*********************************************
 *
 * XSTD_RETURN_IF_ERROR(res)
 *   Evaluates `res` once. If it represents an error, immediately returns
 *   that XSTD_Result value from the calling function.
 *   Use in functions whose return type is XSTD_Result.
 *
 * XSTD_THROW_IF_ERROR(res)
 *   Evaluates `res` once. If it represents an error, converts the error
 *   code to its name string and throws std::runtime_error.
 *
 * XSTD_REPORT_ERROR(errCode)
 *   Convenience wrapper: returns an XSTD_Result carrying `errCode`.
 */
#define XSTD_RETURN_IF_ERROR(res)               \
    do {                                        \
        XSTD_Result _xstd_r = (res);            \
        if (XSTD_isError(_xstd_r))              \
            return _xstd_r;                     \
    } while (0)

#define XSTD_THROW_IF_ERROR(res)                                           \
    do {                                                                   \
        XSTD_Result _xstd_r = (res);                                       \
        if (XSTD_isError(_xstd_r))                                         \
            throw std::runtime_error(XSTD_ErrorCode_toString(_xstd_r));    \
    } while (0)

/*
 * XSTD_REPORT_ERROR(errCode)
 *   Convenience wrapper: returns an XSTD_Result carrying `errCode`.
 *   Must be used inside a function whose return type is XSTD_Result.
 */
#define XSTD_REPORT_ERROR(errCode) \
    return XSTD_returnError(errCode)

/*-*********************************************
 *  XstdError — typed exception for structured error propagation
 *-*********************************************
 *
 * Thrown by internal helpers so that the public API boundary can catch
 * a single type and extract the XSTD_ErrorCode without string inspection.
 *
 * Construction:
 *   throw XstdError(kChecksumMismatch);
 *   throw XstdError(kInvalidArgument, "key must be 32 bytes");
 *
 * Macros:
 *   XSTD_THROW_ERROR(errCode)          — throw XstdError(errCode)
 *   XSTD_THROW_ERROR_MSG(errCode, msg) — throw XstdError(errCode, msg)
 */
class XstdError : public std::runtime_error {
public:
    explicit XstdError(XSTD_ErrorCode code)
        : std::runtime_error(XSTD_ErrorCode_toString(
              static_cast<XSTD_Result>(code)))
        , code_(code) {}

    XstdError(XSTD_ErrorCode code, const std::string& detail)
        : std::runtime_error(
              std::string(XSTD_ErrorCode_toString(
                  static_cast<XSTD_Result>(code))) + " — " + detail)
        , code_(code) {}

    [[nodiscard]] XSTD_ErrorCode code() const noexcept { return code_; }

private:
    XSTD_ErrorCode code_;
};

/*
 * XSTD_THROW_ERROR(errCode)
 *   Throw a typed XstdError carrying `errCode`.
 *
 * XSTD_THROW_ERROR_MSG(errCode, msg)
 *   Throw a typed XstdError with an additional detail string.
 */
#define XSTD_THROW_ERROR(errCode) \
    throw ::xstd::XstdError(errCode)

#define XSTD_THROW_ERROR_MSG(errCode, msg) \
    throw ::xstd::XstdError((errCode), (msg))

} // namespace xstd

#endif