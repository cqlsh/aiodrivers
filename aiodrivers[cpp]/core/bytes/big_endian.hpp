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

#include <bit>
#include <cstdint>
#include <cstring>

namespace aiodrivers::core::bytes {

/**
 * @brief Reads unsigned big-endian integers, the byte order of the SQLite file format.
 * @details Nothing here checks bounds.
 */
class BigEndian {
 public:
  BigEndian() = delete;

  /**
   * @brief Reads a 16-bit big-endian integer.
   * @param source Pointer to two readable bytes.
   * @return The value in native byte order.
   */
  [[nodiscard]] static std::uint16_t ReadUInt16(const std::uint8_t* source) noexcept {
    std::uint16_t value = 0;
    std::memcpy(&value, source, sizeof(value));
    if constexpr (std::endian::native == std::endian::little) {
      value = static_cast<std::uint16_t>((value << 8) | (value >> 8));
    }
    return value;
  }

  /**
   * @brief Reads a 32-bit big-endian integer.
   * @param source Pointer to four readable bytes.
   * @return The value in native byte order.
   */
  [[nodiscard]] static std::uint32_t ReadUInt32(const std::uint8_t* source) noexcept {
    std::uint32_t value = 0;
    std::memcpy(&value, source, sizeof(value));
    if constexpr (std::endian::native == std::endian::little) {
      value = (value << 24) | ((value << 8) & 0x00FF0000U) | ((value >> 8) & 0x0000FF00U) | (value >> 24);
    }
    return value;
  }
};

}  // namespace aiodrivers::core::bytes