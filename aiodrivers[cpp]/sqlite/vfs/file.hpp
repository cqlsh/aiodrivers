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

#include <cstdint>
#include <utility>

namespace aiodrivers::sqlite::vfs {

/**
 * @brief Owns an open database file and reads from it at any offset.
 * @details Other processes can still read and write the file while it is open, same as with SQLite. TryRead and TryGetSize
 *          can run on several threads at once; TryOpen, Close and moves must not overlap with any other call. Each platform
 *          implements the file functions in its own source file.
 */
class File {
 public:
  /** @brief Creates an object without an open file. */
  File() noexcept = default;

  ~File() noexcept { Close(); }

  File(const File&) = delete;
  File& operator=(const File&) = delete;

  /**
   * @brief Closes the current file and takes over the file of other.
   * @param other Source; it has no open file afterwards.
   * @return This object.
   */
  File& operator=(File&& other) noexcept {
    if (&other != this) {
      Close();
      m_handle = std::exchange(other.m_handle, CLOSED);
    }
    return *this;
  }

  /**
   * @brief Opens a file for reading and closes the one that was open before.
   * @details If the open fails, the previous file stays open. Windows treats path as UTF-8, POSIX passes the bytes through
   *          unchanged. Folders behave as in SQLite: Windows refuses them with CantOpenIsDir, while POSIX opens them and the
   *          first TryRead fails with IoErrRead. On POSIX a symbolic link as the last component of path gives CantOpen, so
   *          resolve links first, as SQLite does.
   * @param path Path of the file.
   * @retval Ok The file is open.
   * @retval CantOpen The file doesn't exist or can't be opened.
   * @retval CantOpenIsDir path is a folder (Windows only).
   * @retval IoErrNoMem Converting the path to UTF-16 failed (Windows only).
   */
  [[nodiscard]] ResultCode TryOpen(const char* path) noexcept;

  /**
   * @brief Reads sizeInBytes bytes at offset into buffer.
   * @pre The file is open.
   * @param buffer Destination with room for sizeInBytes bytes.
   * @param sizeInBytes Number of bytes to read.
   * @param offset Position in the file to read from.
   * @retval Ok All bytes were read.
   * @retval IoErrShortRead The file ended first; the rest of buffer is filled with zeros.
   * @retval IoErrRead The read failed.
   * @retval IoErrCorruptFs The error points to a damaged disk or file system (POSIX only). SQLite turns this into Corrupt
   *         while a statement runs.
   */
  [[nodiscard]] ResultCode TryRead(std::uint8_t* buffer, std::uint32_t sizeInBytes, std::uint64_t offset) const noexcept;

  /**
   * @brief Gets the current size of the file.
   * @details On POSIX a file of exactly one byte reports size 0, like in SQLite, which writes that byte itself into empty
   *          databases on FAT drives under macOS.
   * @pre The file is open.
   * @param[out] outSizeInBytes The size in bytes; unchanged on failure.
   * @retval Ok The size was read.
   * @retval IoErrFstat The size couldn't be read.
   */
  [[nodiscard]] ResultCode TryGetSize(std::uint64_t& outSizeInBytes) const noexcept;

  /** @brief Closes the file. Does nothing if no file is open. */
  void Close() noexcept;

  /** @brief Checks whether a file is open. */
  [[nodiscard]] bool IsOpen() const noexcept { return m_handle != CLOSED; }

 private:
  static constexpr std::intptr_t CLOSED = -1;

  std::intptr_t m_handle = CLOSED;
};

}  // namespace aiodrivers::sqlite::vfs