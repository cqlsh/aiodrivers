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

#pragma once

#include "sqlite/result_code.hpp"

#include <array>
#include <cstddef>

namespace aiodrivers::sqlite::vfs {

/**
 * @brief Works with paths and with files that aren't open, like the matching parts of SQLite's VFS.
 * @details Each platform implements the functions in its own source file.
 */
class FileSystem {
 public:
  /** @brief Maximum length of a full path in bytes. SQLite keeps 8 more bytes free so that "-journal" still fits. */
#if defined(_WIN32)
  static constexpr std::size_t MAX_FULL_PATH_SIZE_IN_BYTES = 1032;
#else
  static constexpr std::size_t MAX_FULL_PATH_SIZE_IN_BYTES = 504;
#endif

  /** @brief Buffer for a full path plus its terminating zero. */
  using FullPath = std::array<char, MAX_FULL_PATH_SIZE_IN_BYTES + 1>;

  FileSystem() = delete;

  /**
   * @brief Builds the full path SQLite would open for path.
   * @details Windows drops a slash in front of a drive letter or the \\?\ prefix and takes what GetFullPathNameW returns.
   *          POSIX starts a relative path at the current folder, skips "." and empty names, goes up one folder for ".." and
   *          replaces every symbolic link with its target. Names that don't exist are kept, and a result of just "/" counts
   *          as a failure.
   * @param path The path to resolve; UTF-8 on Windows, passed through as bytes on POSIX.
   * @param[out] output The full path, zero-terminated.
   * @retval Ok output holds the full path.
   * @retval CantOpen A lookup failed, the full path is too long, or it is just the root folder.
   * @retval CantOpenFullPath GetFullPathNameW failed (Windows only).
   * @retval IoErrNoMem Out of memory, or converting the path to UTF-16 failed (Windows).
   */
  [[nodiscard]] static ResultCode TryGetFullPath(const char* path, FullPath& output) noexcept;
};

}  // namespace aiodrivers::sqlite::vfs