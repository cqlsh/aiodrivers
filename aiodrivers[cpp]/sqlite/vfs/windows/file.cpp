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

#if defined(_WIN32)

#include "sqlite/vfs/file.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <utility>

namespace aiodrivers::sqlite::vfs {

namespace {

/**
 * @brief Retries failed file calls on Windows the way SQLite does.
 * @details Virus scanners open files for a moment, so some errors clear up on their own. After the first failure up to ten
 *          retries follow, each waiting 25 ms longer than the one before: 25 to 250 ms, 1.375 s in total.
 */
class Retry {
 public:
  Retry() = delete;

  /**
   * @brief Waits before the next attempt if the error is temporary and retries are left.
   * @param error Error code from GetLastError().
   * @param[in,out] attempts Retries so far; incremented when another attempt follows.
   * @return True after waiting, false for a lasting error or once all ten retries are used up.
   */
  [[nodiscard]] static bool WaitForNextAttempt(DWORD error, int& attempts) noexcept {
    if (attempts >= MAX_ATTEMPTS || !IsTemporary(error)) {
      return false;
    }
    ++attempts;
    ::Sleep(static_cast<DWORD>(attempts) * DELAY_IN_MILLISECONDS);
    return true;
  }

 private:
  [[nodiscard]] static bool IsTemporary(DWORD error) noexcept {
    switch (error) {
      case ERROR_ACCESS_DENIED:
      case ERROR_SHARING_VIOLATION:
      case ERROR_LOCK_VIOLATION:
      case ERROR_DEV_NOT_EXIST:
      case ERROR_NETNAME_DELETED:
      case ERROR_SEM_TIMEOUT:
      case ERROR_NETWORK_UNREACHABLE:
        return true;
      default:
        return false;
    }
  }

  static constexpr int MAX_ATTEMPTS = 10;
  static constexpr DWORD DELAY_IN_MILLISECONDS = 25;
};

/** @brief Converts UTF-8 paths to the UTF-16 paths the Windows API expects. */
class WidePath {
 public:
  WidePath() = delete;

  /**
   * @brief Converts a path to UTF-16.
   * @details Invalid UTF-8 becomes U+FFFD, as in SQLite.
   * @param path Zero-terminated UTF-8 path.
   * @param[out] output The zero-terminated UTF-16 path; unchanged on failure.
   * @retval Ok The path was converted.
   * @retval IoErrNoMem The conversion or the allocation failed.
   */
  [[nodiscard]] static ResultCode TryConvert(const char* path, std::unique_ptr<wchar_t[]>& output) noexcept {
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (length <= 0) {
      return ResultCode::IoErrNoMem;
    }
    std::unique_ptr<wchar_t[]> converted(new (std::nothrow) wchar_t[static_cast<std::size_t>(length)]);
    if (converted == nullptr || ::MultiByteToWideChar(CP_UTF8, 0, path, -1, converted.get(), length) != length) {
      return ResultCode::IoErrNoMem;
    }
    output = std::move(converted);
    return ResultCode::Ok;
  }
};

}  // namespace

ResultCode File::TryOpen(const char* path) noexcept {
  std::unique_ptr<wchar_t[]> widePath;
  const ResultCode converted = WidePath::TryConvert(path, widePath);
  if (converted != ResultCode::Ok) {
    return converted;
  }
  const DWORD attributes = ::GetFileAttributesW(widePath.get());
  if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return ResultCode::CantOpenIsDir;
  }
  HANDLE handle = INVALID_HANDLE_VALUE;
  int attempts = 0;
  do {
    handle = ::CreateFileW(widePath.get(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  } while (handle == INVALID_HANDLE_VALUE && Retry::WaitForNextAttempt(::GetLastError(), attempts));
  if (handle == INVALID_HANDLE_VALUE) {
    return ResultCode::CantOpen;
  }
  Close();
  m_handle = reinterpret_cast<std::intptr_t>(handle);
  return ResultCode::Ok;
}

ResultCode File::TryRead(std::uint8_t* buffer, std::uint32_t sizeInBytes, std::uint64_t offset) const noexcept {
  OVERLAPPED position = {};
  position.Offset = static_cast<DWORD>(offset);
  position.OffsetHigh = static_cast<DWORD>(offset >> 32);
  DWORD readInBytes = 0;
  int attempts = 0;
  while (::ReadFile(reinterpret_cast<HANDLE>(m_handle), buffer, sizeInBytes, &readInBytes, &position) == 0) {
    const DWORD error = ::GetLastError();
    if (error == ERROR_HANDLE_EOF) {
      readInBytes = 0;
      break;
    }
    if (!Retry::WaitForNextAttempt(error, attempts)) {
      return ResultCode::IoErrRead;
    }
  }
  if (readInBytes < sizeInBytes) {
    std::memset(buffer + readInBytes, 0, sizeInBytes - readInBytes);
    return ResultCode::IoErrShortRead;
  }
  return ResultCode::Ok;
}

ResultCode File::TryGetSize(std::uint64_t& outSizeInBytes) const noexcept {
  LARGE_INTEGER size = {};
  if (::GetFileSizeEx(reinterpret_cast<HANDLE>(m_handle), &size) == 0) {
    return ResultCode::IoErrFstat;
  }
  outSizeInBytes = static_cast<std::uint64_t>(size.QuadPart);
  return ResultCode::Ok;
}

void File::Close() noexcept {
  if (m_handle == CLOSED) {
    return;
  }
  static_cast<void>(::CloseHandle(reinterpret_cast<HANDLE>(std::exchange(m_handle, CLOSED))));
}

}  // namespace aiodrivers::sqlite::vfs

#endif