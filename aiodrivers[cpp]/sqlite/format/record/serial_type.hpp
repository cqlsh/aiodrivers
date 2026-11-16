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

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace aiodrivers::sqlite::format::record {

/**
 * @brief Decodes record values by their serial type, the number the record header stores for each field.
 * @details 0 is NULL, 1 to 6 are integers of 1, 2, 3, 4, 6 and 8 bytes, 7 is a double, and 8 and 9 are the integers 0 and 1.
 *          SQLite reads the reserved types 10 and 11 as NULL. From 12 on, even types are blobs and odd types are text, each
 *          (type - 12) / 2 bytes long. Nothing is bounds-checked.
 */
class SerialType {
 public:
  /** @brief Storage classes a serial type can stand for. */
  enum class Kind : std::uint8_t {
    Null,
    Integer,
    Real,
    Text,
    Blob
  };

  SerialType() = delete;

  /**
   * @brief Computes how many bytes a value of the given type takes in the record.
   * @param type Serial type from the record header.
   * @return Size of the value in bytes.
   */
  [[nodiscard]] static constexpr std::uint32_t GetSize(std::uint32_t type) noexcept {
    if (type >= SMALL_TYPE_COUNT) {
      return (type - FIRST_VARIABLE_TYPE) / 2;
    }
    return SMALL_SIZES[type];
  }

  /**
   * @brief Looks up the storage class of a serial type.
   * @param type Serial type from the record header.
   * @return The storage class. A Real value can still read as NULL, see TryReadReal().
   */
  [[nodiscard]] static constexpr Kind GetKind(std::uint32_t type) noexcept {
    if (type < FIRST_VARIABLE_TYPE) {
      return FIXED_KINDS[type];
    }
    return (type & 1) == 0 ? Kind::Blob : Kind::Text;
  }

  /**
   * @brief Decodes an integer value.
   * @param type Integer serial type: 1 to 6, 8 or 9.
   * @param bytes Start of the value; GetSize(type) bytes must be readable. Types 8 and 9 don't read it.
   * @return The value, sign-extended to 64 bits.
   */
  [[nodiscard]] static std::int64_t ReadInteger(std::uint32_t type, const std::uint8_t* bytes) noexcept {
    switch (type) {
      case 1:
        return static_cast<std::int8_t>(bytes[0]);
      case 2:
        return static_cast<std::int16_t>(core::bytes::BigEndian::ReadUInt16(bytes));
      case 3:
        return (static_cast<std::int8_t>(bytes[0]) * 65536) | (bytes[1] << 8) | bytes[2];
      case 4:
        return static_cast<std::int32_t>(core::bytes::BigEndian::ReadUInt32(bytes));
      case 5:
        return (static_cast<std::int16_t>(core::bytes::BigEndian::ReadUInt16(bytes)) * 4294967296LL) + core::bytes::BigEndian::ReadUInt32(bytes + 2);
      case 6:
        return static_cast<std::int64_t>(core::bytes::BigEndian::ReadUInt64(bytes));
      default:
        return static_cast<std::int64_t>(type) - 8;
    }
  }

  /**
   * @brief Decodes a double (serial type 7), stored as a big-endian IEEE 754 value.
   * @param bytes Start of the value; 8 bytes must be readable.
   * @param[out] outValue The decoded value; unchanged for NaN.
   * @return False for NaN, which SQLite reads as NULL.
   */
  [[nodiscard]] static bool TryReadReal(const std::uint8_t* bytes, double& outValue) noexcept {
    const auto value = std::bit_cast<double>(core::bytes::BigEndian::ReadUInt64(bytes));
    if (std::isnan(value)) {
      return false;
    }
    outValue = value;
    return true;
  }

 private:
  static constexpr std::uint32_t FIRST_VARIABLE_TYPE = 12;
  static constexpr std::uint32_t SMALL_TYPE_COUNT = 128;

  static constexpr std::array<std::uint8_t, SMALL_TYPE_COUNT> SMALL_SIZES = [] {
    std::array<std::uint8_t, SMALL_TYPE_COUNT> sizes = {0, 1, 2, 3, 4, 6, 8, 8, 0, 0, 0, 0};
    for (std::uint32_t type = FIRST_VARIABLE_TYPE; type < SMALL_TYPE_COUNT; ++type) {
      sizes[type] = static_cast<std::uint8_t>((type - FIRST_VARIABLE_TYPE) / 2);
    }
    return sizes;
  }();

  static constexpr std::array<Kind, FIRST_VARIABLE_TYPE> FIXED_KINDS = {
      Kind::Null,
      Kind::Integer,
      Kind::Integer,
      Kind::Integer,
      Kind::Integer,
      Kind::Integer,
      Kind::Integer,
      Kind::Real,
      Kind::Integer,
      Kind::Integer,
      Kind::Null,
      Kind::Null
  };
};

}  // namespace aiodrivers::sqlite::format::record