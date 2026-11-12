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

#include <array>
#include <cstddef>
#include <cstdint>

namespace aiodrivers::sqlite {

/** @brief Maps result codes to SQLite's English error messages. */
class ResultMessage {
 public:
  ResultMessage() = delete;

  /**
   * @brief Looks up the message sqlite3_errstr() returns for a result code.
   * @details Extended codes share the message of their primary code, except SQLITE_ABORT_ROLLBACK, which has its own.
   * @param code Primary or extended result code.
   * @return A static string; "unknown error" for codes without a message.
   */
  [[nodiscard]] static const char* Get(ResultCode code) noexcept {
    const auto value = static_cast<std::int32_t>(code);
    if (value == ABORT_ROLLBACK) {
      return "abort due to ROLLBACK";
    }
    if (code == ResultCode::Row) {
      return "another row available";
    }
    if (code == ResultCode::Done) {
      return "no more rows available";
    }
    const auto primary = static_cast<std::size_t>(value & 0xFF);
    if (primary < MESSAGES.size() && MESSAGES[primary] != nullptr) {
      return MESSAGES[primary];
    }
    return "unknown error";
  }

 private:
  static constexpr std::int32_t ABORT_ROLLBACK = static_cast<std::int32_t>(ResultCode::Abort) | (2 << 8);

  static constexpr std::array<const char*, 29> MESSAGES = {
      "not an error",
      "SQL logic error",
      nullptr,
      "access permission denied",
      "query aborted",
      "database is locked",
      "database table is locked",
      "out of memory",
      "attempt to write a readonly database",
      "interrupted",
      "disk I/O error",
      "database disk image is malformed",
      "unknown operation",
      "database or disk is full",
      "unable to open database file",
      "locking protocol",
      nullptr,
      "database schema has changed",
      "string or blob too big",
      "constraint failed",
      "datatype mismatch",
      "bad parameter or other API misuse",
      nullptr,
      "authorization denied",
      nullptr,
      "column index out of range",
      "file is not a database",
      "notification message",
      "warning message"
  };
};

}  // namespace aiodrivers::sqlite