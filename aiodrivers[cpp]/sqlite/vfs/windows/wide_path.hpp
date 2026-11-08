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

#if defined(_WIN32)

#include "sqlite/result_code.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstddef>
#include <memory>
#include <new>
#include <utility>

namespace aiodrivers::sqlite::vfs::windows {

/**
 * @brief Converts UTF-8 paths to the UTF-16 paths the Windows API expects.
 * @details Windows only; on other platforms this header is empty.
 */
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

}  // namespace aiodrivers::sqlite::vfs::windows

#endif