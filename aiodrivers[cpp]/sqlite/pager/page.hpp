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
#include "sqlite/vfs/file.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

namespace aiodrivers::sqlite::pager {

/**
 * @brief Holds one page of the database file, followed by PADDING_SIZE zero bytes.
 * @details Not synchronized. The first read allocates the buffer, and later reads reuse it as long as the page size stays
 *          the same. Pages come from the database file itself; a write-ahead log is not looked at yet.
 */
class Page {
 public:
  /**
   * @brief Number of zero bytes behind the page.
   * @details The format parsers can read past the end of a corrupt page: a cell whose two varints start at the last byte
   *          runs 17 bytes beyond it. The padding covers that, rounded up to 32, so such reads stay inside the buffer and
   *          see zeros.
   */
  static constexpr std::size_t PADDING_SIZE = 32;

  /** @brief Creates an object without a page. */
  Page() noexcept = default;

  Page(const Page&) = delete;
  Page& operator=(const Page&) = delete;

  /**
   * @brief Reads a page of the database file.
   * @details Like SQLite, the part of a page that lies past the end of the file reads as zeros, and the read still counts as
   *          successful. Checking pageNumber against the page count is up to the caller, as in SQLite, where the b-tree
   *          layer does it.
   * @pre file is open, and pageSize is a power of two from 512 to 65536.
   * @param file The database file.
   * @param pageNumber Number of the page, starting at 1.
   * @param pageSize Page size of the database in bytes.
   * @retval Ok The page was read.
   * @retval Corrupt pageNumber is 0.
   * @retval NoMem The buffer couldn't be allocated.
   * @retval IoErrRead Reading the file failed.
   * @retval IoErrCorruptFs Reading failed with an error that points to a damaged disk or file system (POSIX only).
   */
  [[nodiscard]] ResultCode TryRead(const vfs::File& file, std::uint32_t pageNumber, std::uint32_t pageSize) noexcept {
    if (pageNumber == 0) {
      return ResultCode::Corrupt;
    }
    if (pageSize != m_size) {
      std::unique_ptr<std::uint8_t[]> bytes(new (std::nothrow) std::uint8_t[static_cast<std::size_t>(pageSize) + PADDING_SIZE]());
      if (bytes == nullptr) {
        return ResultCode::NoMem;
      }
      m_bytes = std::move(bytes);
      m_size = pageSize;
    }
    const std::uint64_t offset = static_cast<std::uint64_t>(pageNumber - 1) * pageSize;
    const ResultCode read = file.TryRead(m_bytes.get(), pageSize, offset);
    return read == ResultCode::IoErrShortRead ? ResultCode::Ok : read;
  }

  /** @brief Returns the page bytes followed by the padding; nullptr until a read has allocated the buffer. */
  [[nodiscard]] const std::uint8_t* GetBytes() const noexcept { return m_bytes.get(); }

 private:
  std::unique_ptr<std::uint8_t[]> m_bytes;
  std::uint32_t m_size = 0;
};

}  // namespace aiodrivers::sqlite::pager