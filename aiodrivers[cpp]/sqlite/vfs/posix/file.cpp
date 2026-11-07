/*
MIT License

Copyright (c) 2026 cqlsh

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#if !defined(_WIN32)

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include "sqlite/vfs/file.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

namespace aiodrivers::sqlite::vfs {

namespace {

/**
 * @brief Opens descriptors for database files the way SQLite does on POSIX.
 * @details Descriptors 0, 1 and 2 belong to the standard streams. If a program closed one of them, open() could hand it out
 *          for the database, and output meant for the terminal would end up in the file. Such a descriptor gets closed again
 *          and /dev/null takes its number for the rest of the process, so the next attempt gets a higher one.
 */
class Descriptor {
 public:
  Descriptor() = delete;

  /**
   * @brief Opens a file read-only and returns a descriptor above 2.
   * @details Retries on EINTR. O_NOFOLLOW makes a symbolic link as the last component of path fail.
   * @param path Path of the file.
   * @param[out] outDescriptor The new descriptor.
   * @return False if the file can't be opened.
   */
  [[nodiscard]] static bool TryOpenForReading(const char* path, int& outDescriptor) noexcept {
    while (true) {
      const int descriptor = ::open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
      if (descriptor > STDERR_FILENO) {
        outDescriptor = descriptor;
        return true;
      }
      if (descriptor >= 0) {
        static_cast<void>(::close(descriptor));
        if (::open("/dev/null", O_RDONLY) < 0) {
          return false;
        }
      } else if (errno != EINTR) {
        return false;
      }
    }
  }
};

/** @brief Maps the errno of a failed read to the result code SQLite returns on POSIX. */
class ReadError {
 public:
  ReadError() = delete;

  /**
   * @brief Picks the result code for a failed read.
   * @param error errno after the failed pread().
   * @return IoErrCorruptFs for ERANGE, EIO, ENXIO and EDEVERR (where defined), which point to a damaged disk or file system;
   *         IoErrRead for anything else.
   */
  [[nodiscard]] static ResultCode ToResultCode(int error) noexcept {
    switch (error) {
      case ERANGE:
      case EIO:
      case ENXIO:
#if defined(EDEVERR)
      case EDEVERR:
#endif
        return ResultCode::IoErrCorruptFs;
      default:
        return ResultCode::IoErrRead;
    }
  }
};

}  // namespace

ResultCode File::TryOpen(const char* path) noexcept {
  int descriptor = -1;
  if (!Descriptor::TryOpenForReading(path, descriptor)) {
    return ResultCode::CantOpen;
  }
  Close();
  m_handle = descriptor;
  return ResultCode::Ok;
}

ResultCode File::TryRead(std::uint8_t* buffer, std::uint32_t sizeInBytes, std::uint64_t offset) const noexcept {
  const int descriptor = static_cast<int>(m_handle);
  std::uint32_t readInBytes = 0;
  while (readInBytes < sizeInBytes) {
    const ssize_t result = ::pread(descriptor, buffer + readInBytes, sizeInBytes - readInBytes, static_cast<off_t>(offset + readInBytes));
    if (result > 0) {
      readInBytes += static_cast<std::uint32_t>(result);
    } else if (result == 0) {
      break;
    } else if (errno != EINTR) {
      return ReadError::ToResultCode(errno);
    }
  }
  if (readInBytes < sizeInBytes) {
    std::memset(buffer + readInBytes, 0, sizeInBytes - readInBytes);
    return ResultCode::IoErrShortRead;
  }
  return ResultCode::Ok;
}

ResultCode File::TryGetSize(std::uint64_t& outSizeInBytes) const noexcept {
  struct stat status = {};
  if (::fstat(static_cast<int>(m_handle), &status) != 0) {
    return ResultCode::IoErrFstat;
  }
  outSizeInBytes = status.st_size == 1 ? 0 : static_cast<std::uint64_t>(status.st_size);
  return ResultCode::Ok;
}

void File::Close() noexcept {
  if (m_handle == CLOSED) {
    return;
  }
  static_cast<void>(::close(static_cast<int>(std::exchange(m_handle, CLOSED))));
}

}  // namespace aiodrivers::sqlite::vfs

#endif