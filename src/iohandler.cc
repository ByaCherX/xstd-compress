#include "iohandler.h"

#include <cstring>
#include <stdexcept>
#include <string>

namespace xstd {

// ===========================================================================
// Helpers / platform shims
// ===========================================================================

namespace {

#ifdef _WIN32

// Convert a narrow-char path to a wide-char string for Win32 APIs.
static std::wstring ToWide(const std::filesystem::path& p)
{
    return p.wstring();
}

// Query the actual file size via the Win32 handle.
static int64_t QueryFileSize(HANDLE h)
{
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz))
        XSTD_THROW_ERROR_MSG(kIOError, "GetFileSizeEx failed");
    return static_cast<int64_t>(sz.QuadPart);
}

#else

// Query the actual file size via fstat.
static int64_t QueryFileSize(int fd)
{
    struct stat st{};
    if (::fstat(fd, &st) != 0)
        XSTD_THROW_ERROR_MSG(kIOError, "fstat failed");
    return static_cast<int64_t>(st.st_size);
}

#endif

} // namespace

// ===========================================================================
// Constructor / destructor / move
// ===========================================================================

IOHandler::IOHandler(const std::filesystem::path& path, OpenMode mode)
    : path_(path), mode_(mode)
{
    OpenFile();
    LockFile();
    if (mode_ == OpenMode::ReadOnly)
        MapFile();
}

IOHandler::~IOHandler() noexcept
{
    UnmapFile();
    UnlockFile();
    CloseFile();
}

IOHandler::IOHandler(IOHandler&& other) noexcept
    : path_       (std::move(other.path_))
    , mode_       (other.mode_)
    , file_size_  (other.file_size_)
    , append_pos_ (other.append_pos_)
#ifdef _WIN32
    , file_handle_(other.file_handle_)
    , map_handle_ (other.map_handle_)
    , map_view_   (other.map_view_)
#else
    , fd_         (other.fd_)
    , map_view_   (other.map_view_)
    , map_size_   (other.map_size_)
#endif
{
#ifdef _WIN32
    other.file_handle_ = INVALID_HANDLE_VALUE;
    other.map_handle_  = nullptr;
    other.map_view_    = nullptr;
#else
    other.fd_       = -1;
    other.map_view_ = nullptr;  // MAP_FAILED
    other.map_size_ = 0;
#endif
}

IOHandler& IOHandler::operator=(IOHandler&& other) noexcept
{
    if (this != &other) {
        UnmapFile();
        UnlockFile();
        CloseFile();

        path_        = std::move(other.path_);
        mode_        = other.mode_;
        file_size_   = other.file_size_;
        append_pos_  = other.append_pos_;
#ifdef _WIN32
        file_handle_ = other.file_handle_;
        map_handle_  = other.map_handle_;
        map_view_    = other.map_view_;
        other.file_handle_ = INVALID_HANDLE_VALUE;
        other.map_handle_  = nullptr;
        other.map_view_    = nullptr;
#else
        fd_          = other.fd_;
        map_view_    = other.map_view_;
        map_size_    = other.map_size_;
        other.fd_       = -1;
        other.map_view_ = nullptr;
        other.map_size_ = 0;
#endif
    }
    return *this;
}

// ===========================================================================
// ReadAt
// ===========================================================================

std::span<const uint8_t> IOHandler::ReadAt(int64_t offset, std::size_t length) const
{
    if (mode_ == OpenMode::WriteOnly)
        XSTD_THROW_ERROR_MSG(kIOError, "ReadAt called on a write-only IOHandler");

    if (offset < 0 || static_cast<int64_t>(offset + static_cast<int64_t>(length)) > file_size_)
        XSTD_THROW_ERROR_MSG(kIOError,
            "ReadAt: range [" + std::to_string(offset) + ", "
            + std::to_string(offset + static_cast<int64_t>(length))
            + ") exceeds file size " + std::to_string(file_size_));

    if (mode_ == OpenMode::ReadOnly) {
        // Zero-copy path: return a view into the memory-mapped region.
        const auto* base = static_cast<const uint8_t*>(map_view_);
        return {base + offset, length};
    }

    // ReadWrite mode: no memory map — use a positional read into the scratch buffer.
    read_buf_.resize(length);

#ifdef _WIN32
    OVERLAPPED ol{};
    ol.Offset     = static_cast<DWORD>(static_cast<uint64_t>(offset)        & 0xFFFF'FFFFu);
    ol.OffsetHigh = static_cast<DWORD>(static_cast<uint64_t>(offset) >> 32  & 0xFFFF'FFFFu);
    DWORD bytes_read = 0;
    BOOL status = ReadFile(file_handle_, read_buf_.data(),
        static_cast<DWORD>(length), &bytes_read, &ol);

    if (!status || bytes_read != static_cast<DWORD>(length))
        XSTD_THROW_ERROR_MSG(kIOError, "ReadAt: ReadFile failed");
#else
    std::size_t remaining = length;
    uint8_t*    ptr       = read_buf_.data();
    off_t       off       = static_cast<off_t>(offset);
    while (remaining > 0) {
        ssize_t n = ::pread(fd_, ptr, remaining, off);
        if (n <= 0) XSTD_THROW_ERROR_MSG(kIOError, "ReadAt: pread failed");
        ptr       += n;
        off       += static_cast<off_t>(n);
        remaining -= static_cast<std::size_t>(n);
    }
#endif

    return {read_buf_.data(), length};
}

// ===========================================================================
// Append / WriteAt
// ===========================================================================

XSTD_Result IOHandler::Append(std::span<const uint8_t> data)
{
    XSTD_Result r = WriteAt(append_pos_, data);
    if (!XSTD_isError(r)) {
        append_pos_ += static_cast<int64_t>(data.size());
        if (append_pos_ > file_size_)
            file_size_ = append_pos_;
    }
    return r;
}

XSTD_Result IOHandler::WriteAt(int64_t offset, std::span<const uint8_t> data)
{
    if (data.empty()) return XSTD_returnSuccess();

#ifdef _WIN32
    OVERLAPPED ol{};
    ol.Offset     = static_cast<DWORD>(static_cast<uint64_t>(offset)        & 0xFFFF'FFFF);
    ol.OffsetHigh = static_cast<DWORD>(static_cast<uint64_t>(offset) >> 32  & 0xFFFF'FFFF);
    DWORD written = 0;
    if (!WriteFile(file_handle_,
                   data.data(),
                   static_cast<DWORD>(data.size()),
                   &written, &ol)
        || written != static_cast<DWORD>(data.size()))
    {
        return XSTD_returnError(kCannotWriteFile);
    }
    // Update tracked size.
    int64_t end = offset + static_cast<int64_t>(data.size());
    if (end > file_size_) file_size_ = end;
#else
    std::size_t remaining = data.size();
    const uint8_t* ptr    = data.data();
    off_t          off    = static_cast<off_t>(offset);

    while (remaining > 0) {
        ssize_t n = ::pwrite(fd_, ptr, remaining, off);
        if (n < 0) return XSTD_returnError(kCannotWriteFile);
        ptr       += n;
        off       += n;
        remaining -= static_cast<std::size_t>(n);
    }
    // Update tracked size.
    int64_t end = offset + static_cast<int64_t>(data.size());
    if (end > file_size_) file_size_ = end;
#endif

    return XSTD_returnSuccess();
}

// ===========================================================================
// SetAppendPosition / Truncate
// ===========================================================================

void IOHandler::SetAppendPosition(int64_t pos)
{
    if (mode_ == OpenMode::ReadOnly)
        XSTD_THROW_ERROR_MSG(kIOError, "SetAppendPosition called on a read-only IOHandler");
    append_pos_ = pos;
}

XSTD_Result IOHandler::Truncate(int64_t size)
{
    if (mode_ == OpenMode::ReadOnly)
        return XSTD_returnError(kIOError);

#ifdef _WIN32
    LARGE_INTEGER li;
    li.QuadPart = size;
    if (!SetFilePointerEx(file_handle_, li, nullptr, FILE_BEGIN))
        return XSTD_returnError(kIOError);
    if (!SetEndOfFile(file_handle_))
        return XSTD_returnError(kIOError);
#else
    if (::ftruncate(fd_, static_cast<off_t>(size)) != 0)
        return XSTD_returnError(kIOError);
#endif

    file_size_ = size;
    if (append_pos_ > size)
        append_pos_ = size;
    return XSTD_returnSuccess();
}

// ===========================================================================
// Flush / Remap
// ===========================================================================

XSTD_Result IOHandler::Flush()
{
#ifdef _WIN32
    if (map_view_)
        FlushViewOfFile(map_view_, 0);
    if (file_handle_ != INVALID_HANDLE_VALUE)
        FlushFileBuffers(file_handle_);
#else
    if (map_view_ && map_view_ != MAP_FAILED && map_size_ > 0)
        ::msync(map_view_, map_size_, MS_SYNC);
    if (fd_ >= 0)
        ::fdatasync(fd_);
#endif
    return XSTD_returnSuccess();
}

XSTD_Result IOHandler::Remap()
{
    if (mode_ == OpenMode::WriteOnly) return XSTD_returnSuccess();

    if (mode_ == OpenMode::ReadWrite) {
        // No memory map; just refresh file_size_ from OS.
#ifdef _WIN32
        file_size_ = QueryFileSize(file_handle_);
#else
        file_size_ = QueryFileSize(fd_);
#endif
        return XSTD_returnSuccess();
    }

    // ReadOnly: full unmap + remap.
    try {
        UnmapFile();
#ifdef _WIN32
        file_size_ = QueryFileSize(file_handle_);
#else
        file_size_ = QueryFileSize(fd_);
#endif
        if (file_size_ > 0) MapFile();
    } XSTD_ERROR_CATCH_HANDLE(kIOError)
    return XSTD_returnSuccess();
}

// ===========================================================================
// Private — OpenFile
// ===========================================================================

void IOHandler::OpenFile()
{
#ifdef _WIN32
    const std::wstring wpath = ToWide(path_);

    DWORD access  = GENERIC_READ;
    DWORD share   = FILE_SHARE_READ;
    DWORD create  = OPEN_EXISTING;
    DWORD flags   = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;

    if (mode_ == OpenMode::WriteOnly) {
        access  = GENERIC_READ | GENERIC_WRITE;
        share   = 0;                    // exclusive
        create  = CREATE_ALWAYS;
        flags   = FILE_ATTRIBUTE_NORMAL;
    } else if (mode_ == OpenMode::ReadWrite) {
        access  = GENERIC_READ | GENERIC_WRITE;
        share   = 0;
        create  = OPEN_EXISTING;
        flags   = FILE_ATTRIBUTE_NORMAL;
    }

    file_handle_ = CreateFileW(wpath.c_str(), access, share,
                               nullptr, create, flags, nullptr);
    if (file_handle_ == INVALID_HANDLE_VALUE)
        XSTD_THROW_ERROR_MSG(kCannotOpenFile,
            "cannot open file: " + path_.string());

    file_size_  = (mode_ == OpenMode::WriteOnly) ? 0 : QueryFileSize(file_handle_);
    append_pos_ = (mode_ == OpenMode::WriteOnly) ? 0 : file_size_;

#else
    int oflag = O_RDONLY;
    if (mode_ == OpenMode::WriteOnly)
        oflag = O_RDWR | O_CREAT | O_TRUNC;
    else if (mode_ == OpenMode::ReadWrite)
        oflag = O_RDWR;

    fd_ = ::open(path_.c_str(), oflag, static_cast<mode_t>(0644));
    if (fd_ < 0)
        XSTD_THROW_ERROR_MSG(kCannotOpenFile,
            "cannot open file: " + path_.string());

    file_size_  = (mode_ == OpenMode::WriteOnly) ? 0 : QueryFileSize(fd_);
    append_pos_ = (mode_ == OpenMode::WriteOnly) ? 0 : file_size_;
#endif
}

// ===========================================================================
// Private — LockFile / UnlockFile
// ===========================================================================

void IOHandler::LockFile()
{
#ifdef _WIN32
    OVERLAPPED ol{};
    DWORD flags = LOCKFILE_FAIL_IMMEDIATELY;
    if (mode_ != OpenMode::ReadOnly)
        flags |= LOCKFILE_EXCLUSIVE_LOCK;

    if (!LockFileEx(file_handle_, flags, 0,
                    MAXDWORD, MAXDWORD, &ol))
        XSTD_THROW_ERROR_MSG(kIOError,
            "cannot lock archive file (another process may have it open): "
            + path_.string());
#else
    struct flock fl{};
    fl.l_type   = (mode_ == OpenMode::ReadOnly) ? F_RDLCK : F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;    // lock entire file

    if (::fcntl(fd_, F_SETLK, &fl) != 0)
        XSTD_THROW_ERROR_MSG(kIOError,
            "cannot lock archive file (another process may have it open): "
            + path_.string());
#endif
}

void IOHandler::UnlockFile() noexcept
{
#ifdef _WIN32
    if (file_handle_ != INVALID_HANDLE_VALUE) {
        OVERLAPPED ol{};
        UnlockFileEx(file_handle_, 0, MAXDWORD, MAXDWORD, &ol);
    }
#else
    if (fd_ >= 0) {
        struct flock fl{};
        fl.l_type   = F_UNLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start  = 0;
        fl.l_len    = 0;
        ::fcntl(fd_, F_SETLK, &fl);
    }
#endif
}

// ===========================================================================
// Private — MapFile / UnmapFile
// ===========================================================================

void IOHandler::MapFile()
{
    if (file_size_ <= 0) return;  // nothing to map (e.g., freshly-created file)

#ifdef _WIN32
    map_handle_ = CreateFileMappingW(file_handle_, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!map_handle_)
        XSTD_THROW_ERROR_MSG(kIOError,
            "CreateFileMapping failed for: " + path_.string());

    map_view_ = MapViewOfFile(map_handle_, FILE_MAP_READ, 0, 0, 0);
    if (!map_view_) {
        CloseHandle(map_handle_);
        map_handle_ = nullptr;
        XSTD_THROW_ERROR_MSG(kIOError,
            "MapViewOfFile failed for: " + path_.string());
    }
#else
    map_size_ = static_cast<size_t>(file_size_);
    map_view_ = ::mmap(nullptr, map_size_, PROT_READ, MAP_SHARED, fd_, 0);
    if (map_view_ == MAP_FAILED) {
        map_view_ = nullptr;
        map_size_ = 0;
        XSTD_THROW_ERROR_MSG(kIOError,
            "mmap failed for: " + path_.string());
    }
    // Hint sequential access pattern to the OS pre-fetcher.
    ::madvise(map_view_, map_size_, MADV_SEQUENTIAL);
#endif
}

void IOHandler::UnmapFile() noexcept
{
#ifdef _WIN32
    if (map_view_) {
        UnmapViewOfFile(map_view_);
        map_view_ = nullptr;
    }
    if (map_handle_) {
        CloseHandle(map_handle_);
        map_handle_ = nullptr;
    }
#else
    if (map_view_ && map_view_ != MAP_FAILED) {
        ::munmap(map_view_, map_size_);
        map_view_ = nullptr;
        map_size_ = 0;
    }
#endif
}

// ===========================================================================
// Private — CloseFile
// ===========================================================================

void IOHandler::CloseFile() noexcept
{
#ifdef _WIN32
    if (file_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_handle_);
        file_handle_ = INVALID_HANDLE_VALUE;
    }
#else
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
}

} // namespace xstd
