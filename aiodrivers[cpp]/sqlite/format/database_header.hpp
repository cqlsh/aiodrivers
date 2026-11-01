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
#include <cstring>

namespace aiodrivers::sqlite::format {

/**
 * @brief Reads the 100-byte header at the start of a database file.
 * @details This is a view: the bytes aren't copied and must outlive the object. Getters return the stored values unchecked;
 *          HasValidFormat and HasValidUsableSize tell whether the header can be trusted.
 */
class DatabaseHeader {
 public:
  /** @brief Size of the header in bytes, at the start of page 1. */
  static constexpr std::size_t SIZE = 100;

  /**
   * @brief Wraps the header without copying it.
   * @param bytes The first SIZE bytes of the file.
   */
  explicit DatabaseHeader(const std::uint8_t* bytes) noexcept : m_bytes(bytes) {}

  /** @brief Returns the page size in bytes; a stored 1 means 65536. */
  [[nodiscard]] std::uint32_t GetPageSize() const noexcept {
    const std::uint32_t stored = core::bytes::BigEndian::ReadUInt16(m_bytes + Offset::PAGE_SIZE);
    return stored == 1 ? MAX_PAGE_SIZE : stored;
  }

  /** @brief Returns the write version: 1 for rollback journal mode, 2 for WAL. */
  [[nodiscard]] std::uint8_t GetWriteVersion() const noexcept { return m_bytes[Offset::WRITE_VERSION]; }

  /** @brief Returns the read version: 1 for rollback journal mode, 2 for WAL. */
  [[nodiscard]] std::uint8_t GetReadVersion() const noexcept { return m_bytes[Offset::READ_VERSION]; }

  /** @brief Returns the number of bytes reserved at the end of each page. */
  [[nodiscard]] std::uint8_t GetReservedSpace() const noexcept { return m_bytes[Offset::RESERVED_SPACE]; }

  /** @brief Returns the usable bytes per page: the page size minus the reserved space. */
  [[nodiscard]] std::uint32_t GetUsableSize() const noexcept { return GetPageSize() - GetReservedSpace(); }

  /** @brief Returns the file change counter. */
  [[nodiscard]] std::uint32_t GetChangeCounter() const noexcept { return ReadUInt32(Offset::CHANGE_COUNTER); }

  /** @brief Returns the database size in pages as stored in the header. Only trust it if IsPageCountValid() says so. */
  [[nodiscard]] std::uint32_t GetPageCount() const noexcept { return ReadUInt32(Offset::PAGE_COUNT); }

  /** @brief Returns the first freelist trunk page, or 0 if the freelist is empty. */
  [[nodiscard]] std::uint32_t GetFirstFreelistPage() const noexcept { return ReadUInt32(Offset::FIRST_FREELIST_PAGE); }

  /** @brief Returns the total number of freelist pages. */
  [[nodiscard]] std::uint32_t GetFreelistPageCount() const noexcept { return ReadUInt32(Offset::FREELIST_PAGE_COUNT); }

  /** @brief Returns the schema cookie, which is incremented on every schema change. */
  [[nodiscard]] std::uint32_t GetSchemaCookie() const noexcept { return ReadUInt32(Offset::SCHEMA_COOKIE); }

  /** @brief Returns the schema format number. */
  [[nodiscard]] std::uint32_t GetSchemaFormat() const noexcept { return ReadUInt32(Offset::SCHEMA_FORMAT); }

  /** @brief Returns the suggested page cache size, or 0 if none was set. */
  [[nodiscard]] std::uint32_t GetDefaultCacheSize() const noexcept { return ReadUInt32(Offset::DEFAULT_CACHE_SIZE); }

  /** @brief Returns the largest root page when auto-vacuum is on, otherwise 0. */
  [[nodiscard]] std::uint32_t GetLargestRootPage() const noexcept { return ReadUInt32(Offset::LARGEST_ROOT_PAGE); }

  /** @brief Returns the text encoding: 1 for UTF-8, 2 for UTF-16le, 3 for UTF-16be, 0 in a file without tables. */
  [[nodiscard]] std::uint32_t GetTextEncoding() const noexcept { return ReadUInt32(Offset::TEXT_ENCODING); }

  /** @brief Returns the value set with PRAGMA user_version. */
  [[nodiscard]] std::int32_t GetUserVersion() const noexcept { return static_cast<std::int32_t>(ReadUInt32(Offset::USER_VERSION)); }

  /** @brief Returns the value set with PRAGMA application_id. */
  [[nodiscard]] std::int32_t GetApplicationId() const noexcept { return static_cast<std::int32_t>(ReadUInt32(Offset::APPLICATION_ID)); }

  /** @brief Returns the change counter from the time the page count and the library version were last written. */
  [[nodiscard]] std::uint32_t GetVersionValidFor() const noexcept { return ReadUInt32(Offset::VERSION_VALID_FOR); }

  /** @brief Returns the version number of the SQLite library that last wrote the file, e.g. 3049001 for 3.49.1. */
  [[nodiscard]] std::uint32_t GetLibraryVersion() const noexcept { return ReadUInt32(Offset::LIBRARY_VERSION); }

  /** @brief Checks whether incremental-vacuum mode is on. */
  [[nodiscard]] bool IsIncrementalVacuum() const noexcept { return ReadUInt32(Offset::INCREMENTAL_VACUUM) != 0; }

  /** @brief Checks whether the stored page count can be trusted: it is nonzero and was written with the current change counter. */
  [[nodiscard]] bool IsPageCountValid() const noexcept { return GetPageCount() != 0 && GetChangeCounter() == GetVersionValidFor(); }

  /**
   * @brief Checks the magic string, the read version, the payload fractions and the page size.
   * @details SQLite rejects a file that fails here with "file is not a database". The usable size has a check of its own
   *          (HasValidUsableSize), because SQLite compares the page count with the file size in between, and that order
   *          decides which error you get.
   * @return True if all four checks pass.
   */
  [[nodiscard]] bool HasValidFormat() const noexcept {
    if (std::memcmp(m_bytes, MAGIC, sizeof(MAGIC)) != 0 || GetReadVersion() > MAX_READ_VERSION) {
      return false;
    }
    if (std::memcmp(m_bytes + Offset::PAYLOAD_FRACTIONS, PAYLOAD_FRACTIONS, sizeof(PAYLOAD_FRACTIONS)) != 0) {
      return false;
    }
    const std::uint32_t pageSize = GetPageSize();
    return pageSize >= MIN_PAGE_SIZE && (pageSize & (pageSize - 1)) == 0;
  }

  /** @brief Checks that every page keeps at least 480 usable bytes; SQLite rejects less with "file is not a database". */
  [[nodiscard]] bool HasValidUsableSize() const noexcept { return GetPageSize() >= GetReservedSpace() + MIN_USABLE_SIZE; }

 private:
  [[nodiscard]] std::uint32_t ReadUInt32(std::size_t offset) const noexcept { return core::bytes::BigEndian::ReadUInt32(m_bytes + offset); }

  struct Offset {
    static constexpr std::size_t PAGE_SIZE = 16;
    static constexpr std::size_t WRITE_VERSION = 18;
    static constexpr std::size_t READ_VERSION = 19;
    static constexpr std::size_t RESERVED_SPACE = 20;
    static constexpr std::size_t PAYLOAD_FRACTIONS = 21;
    static constexpr std::size_t CHANGE_COUNTER = 24;
    static constexpr std::size_t PAGE_COUNT = 28;
    static constexpr std::size_t FIRST_FREELIST_PAGE = 32;
    static constexpr std::size_t FREELIST_PAGE_COUNT = 36;
    static constexpr std::size_t SCHEMA_COOKIE = 40;
    static constexpr std::size_t SCHEMA_FORMAT = 44;
    static constexpr std::size_t DEFAULT_CACHE_SIZE = 48;
    static constexpr std::size_t LARGEST_ROOT_PAGE = 52;
    static constexpr std::size_t TEXT_ENCODING = 56;
    static constexpr std::size_t USER_VERSION = 60;
    static constexpr std::size_t INCREMENTAL_VACUUM = 64;
    static constexpr std::size_t APPLICATION_ID = 68;
    static constexpr std::size_t VERSION_VALID_FOR = 92;
    static constexpr std::size_t LIBRARY_VERSION = 96;
  };

  static constexpr char MAGIC[] = "SQLite format 3";
  static constexpr std::uint8_t PAYLOAD_FRACTIONS[] = {64, 32, 32};
  static constexpr std::uint8_t MAX_READ_VERSION = 2;
  static constexpr std::uint32_t MIN_PAGE_SIZE = 512;
  static constexpr std::uint32_t MAX_PAGE_SIZE = 65536;
  static constexpr std::uint32_t MIN_USABLE_SIZE = 480;

  const std::uint8_t* m_bytes;
};

}  // namespace aiodrivers::sqlite::format