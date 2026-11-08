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

#include <cstdint>

namespace aiodrivers::sqlite {

/**
 * @brief Lists SQLite's result codes with the same numeric values.
 * @details Names drop the SQLITE_ prefix and use PascalCase, so IoErrShortRead is SQLITE_IOERR_SHORT_READ. Extended codes keep
 *          their primary code in the low byte. Only the extended codes the engine returns are listed.
 */
enum class [[nodiscard]] ResultCode : std::int32_t {
  Ok = 0,
  Error = 1,
  Internal = 2,
  Perm = 3,
  Abort = 4,
  Busy = 5,
  Locked = 6,
  NoMem = 7,
  ReadOnly = 8,
  Interrupt = 9,
  IoErr = 10,
  Corrupt = 11,
  NotFound = 12,
  Full = 13,
  CantOpen = 14,
  Protocol = 15,
  Empty = 16,
  Schema = 17,
  TooBig = 18,
  Constraint = 19,
  Mismatch = 20,
  Misuse = 21,
  NoLfs = 22,
  Auth = 23,
  Format = 24,
  Range = 25,
  NotADb = 26,
  Notice = 27,
  Warning = 28,
  Row = 100,
  Done = 101,
  IoErrRead = IoErr | (1 << 8),
  IoErrShortRead = IoErr | (2 << 8),
  IoErrFstat = IoErr | (7 << 8),
  IoErrNoMem = IoErr | (12 << 8),
  IoErrCorruptFs = IoErr | (33 << 8),
  CantOpenIsDir = CantOpen | (2 << 8),
  CantOpenFullPath = CantOpen | (3 << 8)
};

}  // namespace aiodrivers::sqlite