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
        case kInvalidArgument:      
            return "InvalidArgument";
        case kAlreadyFinalised:     
            return "AlreadyFinalised";
        case kCannotOpenFile:       
            return "CannotOpenFile";
        case kCannotWriteFile:      
            return "CannotWriteFile";
        case kFileNotFound:         
            return "FileNotFound";
        case kInvalidArchive:       
            return "InvalidArchive";
        case kDecryptionFailed:     
            return "DecryptionFailed";
        case kUnsupportedAlgorithm: 
            return "UnsupportedAlgorithm";
        case kChecksumMismatch:     
            return "ChecksumMismatch";
        case kIOError:              
            return "IOError";
        default:                                  
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

} // namespace xstd

#endif