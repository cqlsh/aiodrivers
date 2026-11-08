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

#include "sqlite/vfs/file_system.hpp"
#include "sqlite/vfs/windows/wide_path.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <array>
#include <memory>
#include <new>

namespace aiodrivers::sqlite::vfs {

namespace {

/**
 * @brief Strips the slash that a URI like file:///C:/data.db leaves in front of a Windows path.
 * @details Like SQLite, it only drops a slash that is followed by a drive letter and a colon or by the \\?\ prefix.
 */
class LeadingSlash {
 public:
  LeadingSlash() = delete;

  /**
   * @brief Skips a leading slash in front of a drive letter or the \\?\ prefix.
   * @param path Zero-terminated UTF-8 path.
   * @return path + 1 if the slash was skipped, otherwise path.
   */
  [[nodiscard]] static const char* Skip(const char* path) noexcept {
    if (path[0] == '/' && (IsDriveLetterAndColon(path + 1) || IsLongPathPrefix(path + 1))) {
      return path + 1;
    }
    return path;
  }

 private:
  [[nodiscard]] static bool IsDriveLetterAndColon(const char* path) noexcept {
    return ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':';
  }

  [[nodiscard]] static bool IsLongPathPrefix(const char* path) noexcept {
    return path[0] == '\\' && path[1] == '\\' && path[2] == '?' && path[3] == '\\';
  }
};

/** @brief Converts a full path from UTF-16 back to UTF-8. */
class NarrowPath {
 public:
  NarrowPath() = delete;

  /**
   * @brief Converts a full path to UTF-8.
   * @param path Zero-terminated UTF-16 path.
   * @param[out] output The zero-terminated UTF-8 path.
   * @retval Ok The path was converted.
   * @retval CantOpen The path doesn't fit into output. SQLite can't open such a path either.
   */
  [[nodiscard]] static ResultCode TryConvert(const wchar_t* path, FileSystem::FullPath& output) noexcept {
    const int written = ::WideCharToMultiByte(CP_UTF8, 0, path, -1, output.data(), static_cast<int>(output.size()), nullptr, nullptr);
    return written > 0 ? ResultCode::Ok : ResultCode::CantOpen;
  }
};

}  // namespace

ResultCode FileSystem::TryGetFullPath(const char* path, FullPath& output) noexcept {
  std::unique_ptr<wchar_t[]> widePath;
  const ResultCode converted = windows::WidePath::TryConvert(LeadingSlash::Skip(path), widePath);
  if (converted != ResultCode::Ok) {
    return converted;
  }
  std::array<wchar_t, MAX_FULL_PATH_SIZE_IN_BYTES + 1> fullPath;
  const DWORD needed = ::GetFullPathNameW(widePath.get(), static_cast<DWORD>(fullPath.size()), fullPath.data(), nullptr);
  if (needed == 0) {
    return ResultCode::CantOpenFullPath;
  }
  if (needed < fullPath.size()) {
    return NarrowPath::TryConvert(fullPath.data(), output);
  }
  const DWORD largerSize = needed + 3;
  const std::unique_ptr<wchar_t[]> larger(new (std::nothrow) wchar_t[largerSize]);
  if (larger == nullptr) {
    return ResultCode::IoErrNoMem;
  }
  const DWORD length = ::GetFullPathNameW(widePath.get(), largerSize, larger.get(), nullptr);
  if (length == 0) {
    return ResultCode::CantOpenFullPath;
  }
  return length < largerSize ? NarrowPath::TryConvert(larger.get(), output) : ResultCode::CantOpen;
}

}  // namespace aiodrivers::sqlite::vfs

#endif