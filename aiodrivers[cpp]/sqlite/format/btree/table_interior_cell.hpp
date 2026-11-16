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

#include "core/bytes/big_endian.hpp"
#include "core/platform/force_inline.hpp"
#include "sqlite/format/varint.hpp"

#include <cstddef>
#include <cstdint>

namespace aiodrivers::sqlite::format::btree {

/**
 * @brief Reads a table interior cell: the page number of a left child and a rowid key.
 * @details A view over the page; nothing is parsed up front. Rows under the left child have rowids up to and including the
 *          key, rows further right have larger ones.
 */
class TableInteriorCell {
 public:
  /** @brief Size of the child page number at the start of the cell. */
  static constexpr std::size_t CHILD_POINTER_SIZE = 4;

  /** @brief Maximum size of a cell in bytes. */
  static constexpr std::size_t MAX_SIZE = CHILD_POINTER_SIZE + Varint::MAX_SIZE;

  /**
   * @brief Wraps the cell without parsing it.
   * @param bytes Start of the cell; MAX_SIZE bytes must be readable.
   */
  explicit TableInteriorCell(const std::uint8_t* bytes) noexcept : m_bytes(bytes) {}

  /** @brief Returns the left child page. */
  [[nodiscard]] std::uint32_t GetLeftChild() const noexcept { return core::bytes::BigEndian::ReadUInt32(m_bytes); }

  /** @brief Decodes the rowid key. The value isn't cached, so every call decodes it again. */
  [[nodiscard]] AIODRIVERS_FORCE_INLINE std::int64_t GetRowid() const noexcept {
    std::uint64_t rowid = 0;
    static_cast<void>(Varint::Read(m_bytes + CHILD_POINTER_SIZE, rowid));
    return static_cast<std::int64_t>(rowid);
  }

 private:
  const std::uint8_t* m_bytes;
};

}  // namespace aiodrivers::sqlite::format::btree