#pragma once

// ---------------------------------------------------------------------------
// iohandler.h — Low-level I/O abstraction with memory-mapped reads,
//               positional writes, and advisory file locking.
//
// Two operating modes:
//
//   ReadOnly   — Opens an existing file for reading.
//                The file is memory-mapped; ReadAt() returns zero-copy spans
//                backed directly by the OS page cache.
//
//   WriteOnly  — Creates (or truncates) a file for sequential writing.
//                Append() advances an internal cursor; WriteAt() enables
//                random-access patches (e.g. flag byte back-patch).
//                File is NOT memory-mapped.
//
//   ReadWrite  — Opens an existing file for in-place patching.
//                Supports both ReadAt() and WriteAt(); Remap() refreshes
//                the mapping after writes.
//
// File locking:
//   Windows : LockFileEx — shared for ReadOnly, exclusive for Write modes.
//   POSIX   : fcntl(F_SETLK) — F_RDLCK / F_WRLCK equivalently.
//
// The lock is held for the lifetime of the IOHandler object and released
// on destruction, preventing another process from modifying the archive
// while it is open.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "xstd_errors.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>

// Undefine Win32 macros that conflict with our class method names.
#  ifdef DeleteFile
#    undef DeleteFile
#  endif
#  ifdef MoveFile
#    undef MoveFile
#  endif
#  ifdef CreateFile
#    undef CreateFile
#  endif
#  ifdef ReadFile
#    undef ReadFile
#  endif
#  ifdef WriteFile
#    undef WriteFile
#  endif
#else
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace xstd {

class IOHandler {
public:
    enum class OpenMode {
        ReadOnly,   ///< Existing file; memory-mapped; ReadAt() only.
        WriteOnly,  ///< Created/truncated; Append() + WriteAt() only.
        ReadWrite,  ///< Existing file; ReadAt() + WriteAt(); use Remap() after writes.
    };

    /// Construct and immediately open the file.
    /// Throws XstdError(kCannotOpenFile) if the file cannot be opened.
    /// Throws XstdError(kIOError) if locking or mapping fails.
    explicit IOHandler(const std::filesystem::path& path, OpenMode mode);
    ~IOHandler() noexcept;

    // Non-copyable, movable.
    IOHandler(const IOHandler&)            = delete;
    IOHandler& operator=(const IOHandler&) = delete;
    IOHandler(IOHandler&&) noexcept;
    IOHandler& operator=(IOHandler&&) noexcept;

    // -----------------------------------------------------------------------
    // Read (ReadOnly or ReadWrite mode)
    // -----------------------------------------------------------------------

    /// Returns a zero-copy view into the memory-mapped region.
    /// Valid until the next Remap() call or destruction.
    /// Throws XstdError(kIOError) if offset/length are out of range.
    [[nodiscard]] std::span<const uint8_t> ReadAt(int64_t offset, std::size_t length) const;

    // -----------------------------------------------------------------------
    // Write (WriteOnly or ReadWrite mode)
    // -----------------------------------------------------------------------

    /// Append `data` at the current logical end-of-file (AppendPosition).
    /// Advances the append cursor by `data.size()`.
    XSTD_Result Append(std::span<const uint8_t> data);

    /// Write `data` at an arbitrary byte offset without moving the append cursor.
    /// Useful for patching a previously-written record (flag bytes, etc.).
    XSTD_Result WriteAt(int64_t offset, std::span<const uint8_t> data);

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

    /// Byte offset where the next Append() will write.
    [[nodiscard]] int64_t AppendPosition() const noexcept { return append_pos_; }

    /// Size of the file on disk (updated after every Append / WriteAt).
    [[nodiscard]] int64_t FileSize()  const noexcept { return file_size_; }

    /// Flush OS write buffers to disk (fsync / FlushFileBuffers).
    XSTD_Result Flush();

    /// Re-map the file after write operations so that ReadAt() reflects
    /// the current file contents.  No-op in WriteOnly mode.
    XSTD_Result Remap();

private:
    std::filesystem::path path_;
    OpenMode              mode_;
    int64_t               file_size_{0};
    int64_t               append_pos_{0};

#ifdef _WIN32
    HANDLE file_handle_{INVALID_HANDLE_VALUE};
    HANDLE map_handle_ {nullptr};
    void*  map_view_   {nullptr};
#else
    int    fd_      {-1};
    void*  map_view_{nullptr};   // MAP_FAILED when not mapped
    size_t map_size_{0};
#endif

    /// Scratch buffer used by ReadAt() in ReadWrite mode (no memory map).
    mutable std::vector<uint8_t> read_buf_;

    void OpenFile();
    void LockFile();
    void MapFile();
    void UnmapFile() noexcept;
    void UnlockFile() noexcept;
    void CloseFile() noexcept;
};

} // namespace xstd
