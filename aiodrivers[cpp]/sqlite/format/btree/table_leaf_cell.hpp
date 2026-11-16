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

#include "core/platform/force_inline.hpp"
#include "sqlite/format/varint.hpp"

#include <cstddef>
#include <cstdint>

namespace aiodrivers::sqlite::format::btree {

/**
 * @brief Parses a table leaf cell, which holds the rowid and the payload of one row.
 * @details The constructor parses the cell once; the object points into the page and copies nothing. A payload too big for
 *          the page keeps its first part in the cell and continues on overflow pages, split by the rules of the file format.
 *          The payload size is decoded like in btreeParseCellPtr: only the low 32 bits are kept, and a 9th varint byte adds
 *          just 7 bits. That only matters for corrupt files, but it keeps us in line with SQLite there.
 */
class TableLeafCell {
 public:
  /** @brief Maximum size of the two varints at the start of a cell. */
  static constexpr std::size_t MAX_PREFIX_SIZE = 2 * Varint::MAX_SIZE;

  /**
   * @brief Parses the cell at bytes.
   * @param bytes Start of the cell; MAX_PREFIX_SIZE bytes must be readable.
   * @param usableSize Usable size of the page, which decides how much of the payload stays in the cell.
   */
  AIODRIVERS_FORCE_INLINE TableLeafCell(const std::uint8_t* bytes, std::uint32_t usableSize) noexcept {
    std::uint32_t payloadSize = 0;
    const std::uint8_t* payload = bytes + ReadPayloadSize(bytes, payloadSize);
    std::uint64_t rowid = 0;
    payload += Varint::Read(payload, rowid);
    m_payload = payload;
    m_rowid = static_cast<std::int64_t>(rowid);
    m_payloadSize = payloadSize;
    m_localPayloadSize = ToLocalPayloadSize(payloadSize, usableSize);
  }

  /** @brief Returns the rowid. */
  [[nodiscard]] std::int64_t GetRowid() const noexcept { return m_rowid; }

  /** @brief Returns the total payload size in bytes, including the part on overflow pages. */
  [[nodiscard]] std::uint32_t GetPayloadSize() const noexcept { return m_payloadSize; }

  /** @brief Returns the start of the payload; only the first GetLocalPayloadSize() bytes are in the cell. */
  [[nodiscard]] const std::uint8_t* GetPayload() const noexcept { return m_payload; }

  /** @brief Returns the number of payload bytes stored in the cell itself. */
  [[nodiscard]] std::uint32_t GetLocalPayloadSize() const noexcept { return m_localPayloadSize; }

 private:
  [[nodiscard]] static AIODRIVERS_FORCE_INLINE std::size_t ReadPayloadSize(const std::uint8_t* source, std::uint32_t& outValue) noexcept {
    std::uint64_t value = 0;
    const std::size_t size = Varint::Read(source, value);
    if (size == Varint::MAX_SIZE) {
      value = ((value >> 8) << 7) | (value & LOW_BITS);
    }
    outValue = static_cast<std::uint32_t>(value);
    return size;
  }

  [[nodiscard]] static std::uint32_t ToLocalPayloadSize(std::uint32_t payloadSize, std::uint32_t usableSize) noexcept {
    const std::uint32_t maxLocal = usableSize - 35;
    if (payloadSize <= maxLocal) {
      return payloadSize;
    }
    const std::uint32_t minLocal = ((usableSize - 12) * 32 / 255) - 23;
    const std::uint32_t local = minLocal + ((payloadSize - minLocal) % (usableSize - 4));
    return local <= maxLocal ? local : minLocal;
  }

  static constexpr std::uint64_t LOW_BITS = 0x7F;

  const std::uint8_t* m_payload = nullptr;
  std::int64_t m_rowid = 0;
  std::uint32_t m_payloadSize = 0;
  std::uint32_t m_localPayloadSize = 0;
};

}  // namespace aiodrivers::sqlite::format::btree