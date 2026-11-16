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

#include <cstddef>
#include <cstdint>

namespace aiodrivers::sqlite::format::btree {

/**
 * @brief Reads the header of a b-tree page and the cell pointer array behind it.
 * @details A view over the page; nothing is copied. The header starts at offset 0, except on page 1, where it follows the
 *          100-byte database header. Getters don't validate anything; HasValidType and HasValidCellCount tell whether SQLite
 *          would accept the page.
 */
class PageHeader {
 public:
  /** @brief Page types as stored in the first byte of the header. */
  enum class Type : std::uint8_t {
    IndexInterior = 2,
    TableInterior = 5,
    IndexLeaf = 10,
    TableLeaf = 13
  };

  /**
   * @brief Wraps the header without copying it.
   * @param bytes Start of the header: offset 100 on page 1, offset 0 on every other page.
   */
  explicit PageHeader(const std::uint8_t* bytes) noexcept : m_bytes(bytes) {}

  /** @brief Returns the page type. It can be any byte value unless HasValidType() passed. */
  [[nodiscard]] Type GetType() const noexcept { return static_cast<Type>(m_bytes[Offset::TYPE]); }

  /** @brief Returns the number of cells on the page. */
  [[nodiscard]] std::uint16_t GetCellCount() const noexcept { return ReadUInt16(Offset::CELL_COUNT); }

  /** @brief Returns the right-most child page. Only interior pages store it. */
  [[nodiscard]] std::uint32_t GetRightChild() const noexcept { return core::bytes::BigEndian::ReadUInt32(m_bytes + Offset::RIGHT_CHILD); }

  /** @brief Returns the size of the header: 8 bytes on leaf pages, 12 on interior pages. */
  [[nodiscard]] std::size_t GetSize() const noexcept { return IsLeaf() ? LEAF_SIZE : INTERIOR_SIZE; }

  /**
   * @brief Returns the offset of a cell from the start of the page, as stored in the cell pointer array.
   * @details The value comes back unchecked. Mask it with pageSize - 1 before using it, like SQLite's findCell() does.
   * @param index Index of the cell; must be below GetCellCount().
   * @return The stored offset.
   */
  [[nodiscard]] std::uint16_t GetCellOffset(std::size_t index) const noexcept { return ReadUInt16(GetSize() + (index * CELL_POINTER_SIZE)); }

  /** @brief Checks whether this is a leaf page. */
  [[nodiscard]] bool IsLeaf() const noexcept {
    const Type type = GetType();
    return type == Type::TableLeaf || type == Type::IndexLeaf;
  }

  /** @brief Checks whether this is a table page, keyed by rowid; false for index pages. */
  [[nodiscard]] bool IsTable() const noexcept {
    const Type type = GetType();
    return type == Type::TableLeaf || type == Type::TableInterior;
  }

  /** @brief Checks whether the type is one of the four known values; SQLite reports anything else as corrupt. */
  [[nodiscard]] bool HasValidType() const noexcept {
    const Type type = GetType();
    return type == Type::IndexInterior || type == Type::TableInterior || type == Type::IndexLeaf || type == Type::TableLeaf;
  }

  /**
   * @brief Checks whether the cell count can fit on the page.
   * @details Each cell takes at least 4 bytes plus its 2-byte pointer, so a page holds at most (pageSize - 8) / 6 cells.
   *          SQLite reports a larger count as corrupt.
   * @param pageSize Page size in bytes. SQLite uses the full page size here, not the usable size.
   * @return True if SQLite would accept the count.
   */
  [[nodiscard]] bool HasValidCellCount(std::uint32_t pageSize) const noexcept {
    return GetCellCount() <= (pageSize - LEAF_SIZE) / (MIN_CELL_SIZE + CELL_POINTER_SIZE);
  }

 private:
  [[nodiscard]] std::uint16_t ReadUInt16(std::size_t offset) const noexcept { return core::bytes::BigEndian::ReadUInt16(m_bytes + offset); }

  struct Offset {
    static constexpr std::size_t TYPE = 0;
    static constexpr std::size_t CELL_COUNT = 3;
    static constexpr std::size_t RIGHT_CHILD = 8;
  };

  static constexpr std::size_t LEAF_SIZE = 8;
  static constexpr std::size_t INTERIOR_SIZE = 12;
  static constexpr std::size_t CELL_POINTER_SIZE = 2;
  static constexpr std::size_t MIN_CELL_SIZE = 4;

  const std::uint8_t* m_bytes;
};

}  // namespace aiodrivers::sqlite::format::btree