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

#include "sqlite/format/record/serial_type.hpp"
#include "sqlite/format/varint.hpp"
#include "sqlite/result_code.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace aiodrivers::sqlite::format::record {

/**
 * @brief Parses a record header and validates it the way SQLite's OP_Column does.
 * @details A record starts with its header size as a varint, followed by one serial type per field and then the values in
 *          field order. SQLite parses the header lazily, only up to the last column a statement reads, so TryParse stops
 *          after as many fields as the caller asks for.
 */
class Header {
 public:
  /** @brief One parsed field of the header. */
  struct Field {
    /** @brief Serial type of the field. */
    std::uint32_t m_type = 0;

    /** @brief Offset of the value from the start of the payload. */
    std::uint32_t m_offset = 0;
  };

  /** @brief Largest header size SQLite accepts, in bytes. */
  static constexpr std::uint32_t MAX_SIZE = 98307;

  Header() = delete;

  /**
   * @brief Parses the serial types and value offsets of the first fields.size() fields.
   * @details Matches OP_Column, quirks included. The header size is only checked against payloadSize and MAX_SIZE when the
   *          header doesn't fit into localSize (SQLite's slow path), and that path also accepts a header that holds nothing
   *          but its own size. A header size of 0 means a record without fields, which SQLite accepts for historical reasons.
   *          An empty span parses nothing, like a statement that reads no column.
   * @pre The whole header plus Varint::MAX_SIZE more bytes must be readable at payload.
   * @param payload Start of the record.
   * @param payloadSize Size of the whole record in bytes.
   * @param localSize Payload bytes on the page (SQLite's szRow), cut off at the end of the page.
   * @param[out] fields Receives one entry per parsed field.
   * @param[out] outCount Number of parsed fields; 0 on failure.
   * @retval Ok The parsed fields are valid. There can be fewer than fields.size() if the record has fewer fields.
   * @retval Corrupt SQLite would report the record as corrupt.
   */
  [[nodiscard]] static ResultCode TryParse(
      const std::uint8_t* payload, std::uint32_t payloadSize, std::uint32_t localSize, std::span<Field> fields, std::uint32_t& outCount
  ) noexcept {
    outCount = 0;
    if (fields.empty()) {
      return ResultCode::Ok;
    }
    std::uint32_t headerSize = 0;
    const std::size_t varintSize = Varint::ReadClamped(payload, headerSize);
    if (localSize < headerSize) {
      if (headerSize > payloadSize || headerSize > MAX_SIZE) {
        return ResultCode::Corrupt;
      }
      if (varintSize >= headerSize) {
        return ResultCode::Ok;
      }
    }
    const std::uint8_t* cursor = payload + varintSize;
    const std::uint8_t* const end = payload + headerSize;
    std::uint64_t offset = headerSize;
    std::size_t count = 0;
    do {
      std::uint32_t type = *cursor;
      if (type < CONTINUATION_BIT) {
        ++cursor;
      } else {
        cursor += Varint::ReadClamped(cursor, type);
      }
      fields[count].m_type = type;
      fields[count].m_offset = static_cast<std::uint32_t>(offset);
      offset += SerialType::GetSize(type);
      ++count;
    } while (count < fields.size() && cursor < end);
    const bool headerOverrun = cursor > end;
    const bool valueSizeMismatch = cursor == end && offset != payloadSize;
    const bool payloadOverrun = offset > payloadSize;
    if (headerOverrun || valueSizeMismatch || payloadOverrun) {
      return headerSize == 0 ? ResultCode::Ok : ResultCode::Corrupt;
    }
    outCount = static_cast<std::uint32_t>(count);
    return ResultCode::Ok;
  }

 private:
  static constexpr std::uint32_t CONTINUATION_BIT = 0x80;
};

}  // namespace aiodrivers::sqlite::format::record