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

#include "sqlite/format/database_header.hpp"
#include "sqlite/result_code.hpp"
#include "sqlite/vfs/file.hpp"
#include "sqlite/vfs/file_system.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace aiodrivers::sqlite {

/**
 * @brief Holds an open database, like a SQLite connection does. Read-only for now.
 * @details TryReadHeader can run on several threads at once; TryOpen and Close must not overlap with any other call.
 */
class Database {
 public:
  /** @brief Copy of the 100-byte header at the start of the database file. */
  using Header = std::array<std::uint8_t, format::DatabaseHeader::SIZE>;

  /** @brief Creates an object without an open database. */
  Database() noexcept = default;

  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  /**
   * @brief Opens a database for reading and closes the one that was open before.
   * @details Like SQLite, it resolves the full path first and reads the header once while opening, so an unreadable file
   *          already fails here. A file shorter than the header still opens. If anything fails, the previous database stays
   *          open.
   * @param path Path of the database file.
   * @return Ok, or the error from FileSystem::TryGetFullPath, File::TryOpen or File::TryRead.
   */
  [[nodiscard]] ResultCode TryOpen(const char* path) noexcept {
    vfs::FileSystem::FullPath fullPath;
    const ResultCode resolved = vfs::FileSystem::TryGetFullPath(path, fullPath);
    if (resolved != ResultCode::Ok) {
      return resolved;
    }
    vfs::File file;
    const ResultCode opened = file.TryOpen(fullPath.data());
    if (opened != ResultCode::Ok) {
      return opened;
    }
    Header header;
    const ResultCode read = file.TryRead(header.data(), static_cast<std::uint32_t>(header.size()), 0);
    if (read != ResultCode::Ok && read != ResultCode::IoErrShortRead) {
      return read;
    }
    m_path = fullPath;
    m_file = std::move(file);
    return ResultCode::Ok;
  }

  /**
   * @brief Reads the header again and checks it the way SQLite does before the first statement touches the database.
   * @details An empty file passes as an empty database. The checks run in SQLite's order, which decides the error: format,
   *          then the page count against the file size, then the usable size. A read that fails with IoErrCorruptFs comes
   *          back as Corrupt, the way SQLite's statements report it.
   * @pre The database is open.
   * @param[out] output The header; only written on success.
   * @retval Ok The header is valid.
   * @retval NotADb Wrong format, or fewer than 480 usable bytes per page.
   * @retval Corrupt The page count in the header goes past the end of the file, or the file system reported damage.
   * @retval IoErrFstat The file size couldn't be read.
   * @retval IoErrRead Reading the header failed.
   */
  [[nodiscard]] ResultCode TryReadHeader(Header& output) const noexcept {
    std::uint64_t sizeInBytes = 0;
    const ResultCode sized = m_file.TryGetSize(sizeInBytes);
    if (sized != ResultCode::Ok) {
      return sized;
    }
    Header header;
    const ResultCode read = m_file.TryRead(header.data(), static_cast<std::uint32_t>(header.size()), 0);
    if (read == ResultCode::IoErrCorruptFs) {
      return ResultCode::Corrupt;
    }
    if (read != ResultCode::Ok && read != ResultCode::IoErrShortRead) {
      return read;
    }
    const ResultCode checked = Check(header, sizeInBytes);
    if (checked == ResultCode::Ok) {
      output = header;
    }
    return checked;
  }

  /** @brief Closes the database. Does nothing if none is open; GetPath() still returns the last path. */
  void Close() noexcept { m_file.Close(); }

  /** @brief Returns the full path of the database, or an empty string if none was ever opened. */
  [[nodiscard]] const char* GetPath() const noexcept { return m_path.data(); }

  /** @brief Checks whether a database is open. */
  [[nodiscard]] bool IsOpen() const noexcept { return m_file.IsOpen(); }

 private:
  [[nodiscard]] static ResultCode Check(const Header& bytes, std::uint64_t fileSizeInBytes) noexcept {
    const format::DatabaseHeader header(bytes.data());
    const std::uint32_t pageCount = header.IsPageCountValid() ? header.GetPageCount() : 0;
    if (pageCount == 0 && fileSizeInBytes == 0) {
      return ResultCode::Ok;
    }
    if (!header.HasValidFormat()) {
      return ResultCode::NotADb;
    }
    const std::uint64_t pageSize = header.GetPageSize();
    if (pageCount > (fileSizeInBytes + pageSize - 1) / pageSize) {
      return ResultCode::Corrupt;
    }
    if (!header.HasValidUsableSize()) {
      return ResultCode::NotADb;
    }
    return ResultCode::Ok;
  }

  vfs::FileSystem::FullPath m_path = {};
  vfs::File m_file;
};

}  // namespace aiodrivers::sqlite