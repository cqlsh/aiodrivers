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

#include <cstddef>
#include <cstdint>
#include <limits>

namespace aiodrivers::sqlite::format {

/**
 * @brief Reads SQLite's variable-length integers.
 * @details A varint takes 1 to 9 bytes, most significant group first. The first 8 bytes contribute their low 7 bits and have
 *          the high bit set if another byte follows; a 9th byte contributes all 8 bits. Nothing is bounds-checked, and since
 *          the length is only known after reading, MAX_SIZE bytes must be readable at source.
 */
class Varint {
 public:
  /** @brief Maximum size of a varint in bytes. */
  static constexpr std::size_t MAX_SIZE = 9;

  Varint() = delete;

  /**
   * @brief Reads a varint as a 64-bit value, with the same results as sqlite3GetVarint().
   * @param source Start of the varint; MAX_SIZE bytes must be readable.
   * @param[out] outValue The decoded value.
   * @return Size of the varint in bytes, 1 to MAX_SIZE.
   */
  [[nodiscard]] static AIODRIVERS_FORCE_INLINE constexpr std::size_t Read(const std::uint8_t* source, std::uint64_t& outValue) noexcept {
    if (IsLast(source[0])) {
      outValue = source[0];
      return 1;
    }
    if (IsLast(source[1])) {
      outValue = (GetLowBits(source[0]) << 7) | source[1];
      return 2;
    }
    if (IsLast(source[2])) {
      outValue = (GetLowBits(source[0]) << 14) | (GetLowBits(source[1]) << 7) | source[2];
      return 3;
    }
    std::uint64_t value = (GetLowBits(source[0]) << 21) | (GetLowBits(source[1]) << 14) | (GetLowBits(source[2]) << 7);
    if (IsLast(source[3])) {
      outValue = value | source[3];
      return 4;
    }
    value = (value | GetLowBits(source[3])) << 7;
    if (IsLast(source[4])) {
      outValue = value | source[4];
      return 5;
    }
    value = (value | GetLowBits(source[4])) << 7;
    if (IsLast(source[5])) {
      outValue = value | source[5];
      return 6;
    }
    value = (value | GetLowBits(source[5])) << 7;
    if (IsLast(source[6])) {
      outValue = value | source[6];
      return 7;
    }
    value = (value | GetLowBits(source[6])) << 7;
    if (IsLast(source[7])) {
      outValue = value | source[7];
      return 8;
    }
    outValue = ((value | GetLowBits(source[7])) << 8) | source[8];
    return MAX_SIZE;
  }

  /**
   * @brief Reads a varint into 32 bits and clamps larger values to 0xFFFFFFFF.
   * @details That's how SQLite's getVarint32 reads the header size and the serial types of a record. Clamping the same way
   *          makes corrupt records fail the same checks.
   * @param source Start of the varint; MAX_SIZE bytes must be readable.
   * @param[out] outValue The decoded value, at most 0xFFFFFFFF.
   * @return Size of the varint in bytes, 1 to MAX_SIZE.
   */
  [[nodiscard]] static AIODRIVERS_FORCE_INLINE constexpr std::size_t ReadClamped(const std::uint8_t* source, std::uint32_t& outValue) noexcept {
    if (IsLast(source[0])) {
      outValue = source[0];
      return 1;
    }
    if (IsLast(source[1])) {
      outValue = static_cast<std::uint32_t>((GetLowBits(source[0]) << 7) | source[1]);
      return 2;
    }
    if (IsLast(source[2])) {
      outValue = static_cast<std::uint32_t>((GetLowBits(source[0]) << 14) | (GetLowBits(source[1]) << 7) | source[2]);
      return 3;
    }
    std::uint64_t value = 0;
    const std::size_t size = Read(source, value);
    outValue = value > MAX_CLAMPED ? MAX_CLAMPED : static_cast<std::uint32_t>(value);
    return size;
  }

 private:
  [[nodiscard]] static constexpr bool IsLast(std::uint8_t byte) noexcept { return byte < CONTINUATION_BIT; }

  [[nodiscard]] static constexpr std::uint64_t GetLowBits(std::uint8_t byte) noexcept { return byte & LOW_BITS; }

  static constexpr std::uint8_t CONTINUATION_BIT = 0x80;
  static constexpr std::uint8_t LOW_BITS = 0x7F;
  static constexpr std::uint32_t MAX_CLAMPED = (std::numeric_limits<std::uint32_t>::max)();
};

}  // namespace aiodrivers::sqlite::format