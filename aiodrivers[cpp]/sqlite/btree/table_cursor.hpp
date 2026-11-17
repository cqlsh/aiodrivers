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

#include "sqlite/btree/payload_reader.hpp"
#include "sqlite/format/btree/page_header.hpp"
#include "sqlite/format/btree/table_interior_cell.hpp"
#include "sqlite/format/btree/table_leaf_cell.hpp"
#include "sqlite/format/database_header.hpp"
#include "sqlite/pager/page.hpp"
#include "sqlite/pager/page_cache.hpp"
#include "sqlite/result_code.hpp"
#include "sqlite/vfs/file.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace aiodrivers::sqlite::btree {

/**
 * @brief Walks the rows of a table b-tree in rowid order, like SQLite's BtCursor on a rowid table.
 * @details The pages from the root down to the current leaf stay pinned in the page cache, at most 20 levels as in SQLite.
 *          Each page gets the checks of SQLite's getAndInitPage, moveToRoot and moveToChild, so a damaged tree gives
 *          Corrupt at the same row as in SQLite. The getters and TryReadPayload need a current row: the last move returned
 *          Ok. After any other result only TryMoveToFirst may follow. Not synchronized.
 */
class TableCursor {
 public:
  /**
   * @brief Creates a cursor over one table; it pins no page until TryMoveToFirst.
   * @param file The database file; must outlive the cursor.
   * @param cache The page cache of that file; must outlive the cursor.
   * @param pageSize Page size in bytes.
   * @param usableSize Usable bytes per page, the page size minus the reserved space.
   * @param pageCount Size of the database in pages, as SQLite's b-tree layer counts it.
   * @param root Root page of the table, as sqlite_schema lists it.
   */
  TableCursor(
      const vfs::File& file, pager::PageCache& cache, std::uint32_t pageSize, std::uint32_t usableSize, std::uint32_t pageCount, std::uint32_t root
  ) noexcept
      : m_cache(&cache),
        m_reader(file, cache, pageSize, usableSize, pageCount),
        m_pageSize(pageSize),
        m_usableSize(usableSize),
        m_pageCount(pageCount),
        m_root(root) {}

  TableCursor(const TableCursor&) = delete;
  TableCursor& operator=(const TableCursor&) = delete;

  /**
   * @brief Moves to the first row, which in an intact tree has the smallest rowid.
   * @details Releases the pages of the previous position first. Root page 1 in a database without pages counts as an empty
   *          table, as in SQLite.
   * @retval Ok The cursor is on the first row.
   * @retval Done The table has no rows.
   * @retval Corrupt The root page is 0 or past the page count, has an unknown type or too many cells, is an index page, or
   *         is an interior page without cells other than page 1; or a page below it is damaged in one of the ways
   *         TryMoveToNext reports.
   * @retval NoMem The page cache couldn't grow.
   * @retval IoErrRead Reading a page failed.
   * @retval IoErrCorruptFs Reading failed with an error that points to a damaged disk or file system (POSIX only).
   */
  [[nodiscard]] ResultCode TryMoveToFirst() noexcept {
    for (Level& level : m_levels) {
      level.m_page.Reset();
    }
    m_depth = 0;
    if (m_root == 1 && m_pageCount == 0) {
      return ResultCode::Done;
    }
    Level& root = m_levels[0];
    const ResultCode loaded = TryLoad(root, m_root);
    if (loaded != ResultCode::Ok) {
      return loaded;
    }
    if (!format::btree::PageHeader(root.m_header).IsTable()) {
      root.m_page.Reset();
      return ResultCode::Corrupt;
    }
    if (root.m_cellCount == 0) {
      if (root.m_isLeaf) {
        return ResultCode::Done;
      }
      if (m_root != 1) {
        return ResultCode::Corrupt;
      }
      const ResultCode moved = TryMoveToChild(format::btree::PageHeader(root.m_header).GetRightChild());
      if (moved != ResultCode::Ok) {
        return moved;
      }
    }
    return TryMoveToLeftmost();
  }

  /**
   * @brief Moves to the next row; in an intact tree that's the one with the next larger rowid.
   * @pre The cursor is on a row.
   * @retval Ok The cursor is on the next row.
   * @retval Done The previous row was the last one.
   * @retval Corrupt A child page is 0 or past the page count, has an unknown type, too many cells or no cells at all, is an
   *         index page, or would be the 21st level.
   * @retval NoMem The page cache couldn't grow.
   * @retval IoErrRead Reading a page failed.
   * @retval IoErrCorruptFs Reading failed with an error that points to a damaged disk or file system (POSIX only).
   */
  [[nodiscard]] ResultCode TryMoveToNext() noexcept {
    for (;;) {
      Level& level = m_levels[m_depth];
      ++level.m_index;
      if (level.m_index < level.m_cellCount) {
        if (level.m_isLeaf) {
          ParseCell();
          return ResultCode::Ok;
        }
        return TryMoveToLeftmost();
      }
      if (!level.m_isLeaf) {
        const ResultCode moved = TryMoveToChild(format::btree::PageHeader(level.m_header).GetRightChild());
        return moved == ResultCode::Ok ? TryMoveToLeftmost() : moved;
      }
      do {
        if (m_depth == 0) {
          return ResultCode::Done;
        }
        m_levels[m_depth].m_page.Reset();
        --m_depth;
      } while (m_levels[m_depth].m_index >= m_levels[m_depth].m_cellCount);
    }
  }

  /** @brief Returns the rowid of the current row. */
  [[nodiscard]] std::int64_t GetRowid() const noexcept { return m_rowid; }

  /** @brief Returns the payload size of the current row, overflow pages included. */
  [[nodiscard]] std::uint32_t GetPayloadSize() const noexcept { return m_payloadSize; }

  /**
   * @brief Returns the part of the payload stored on the page, like sqlite3BtreePayloadFetch.
   * @details The size is the local part of the cell, cut off at the end of the page when a damaged cell runs past it.
   * @param[out] outSizeInBytes Number of bytes readable at the returned pointer.
   * @return Start of the payload.
   */
  [[nodiscard]] const std::uint8_t* GetLocalPayload(std::uint32_t& outSizeInBytes) const noexcept {
    const std::uint8_t* const end = m_levels[m_depth].m_bytes + m_pageSize;
    const std::ptrdiff_t available = std::max<std::ptrdiff_t>(end - m_payload, 0);
    outSizeInBytes = std::min(m_localPayloadSize, static_cast<std::uint32_t>(available));
    return m_payload;
  }

  /**
   * @brief Copies amount bytes of the current row's payload, starting at offset, into output.
   * @details PayloadReader::TryRead does the work and remembers the overflow chain of the row until the cursor moves.
   * @pre offset + amount doesn't exceed the payload size.
   * @param offset Position in the payload to start at.
   * @param amount Number of bytes to copy.
   * @param[out] output Destination with room for amount bytes.
   * @return Ok, or the error from PayloadReader::TryRead.
   */
  [[nodiscard]] ResultCode TryReadPayload(std::uint32_t offset, std::uint32_t amount, std::uint8_t* output) noexcept {
    return m_reader.TryRead(offset, amount, output);
  }

 private:
  struct Level {
    pager::PageCache::Reference m_page;
    const std::uint8_t* m_bytes = nullptr;
    const std::uint8_t* m_header = nullptr;
    std::uint32_t m_cellCount = 0;
    std::uint32_t m_index = 0;
    bool m_isLeaf = false;
  };

  [[nodiscard]] ResultCode TryLoad(Level& level, std::uint32_t pageNumber) noexcept {
    if (pageNumber > m_pageCount) {
      return ResultCode::Corrupt;
    }
    const ResultCode fetched = m_cache->TryFetch(pageNumber, level.m_page);
    if (fetched != ResultCode::Ok) {
      return fetched;
    }
    const std::uint8_t* const bytes = level.m_page.GetPage().GetBytes();
    const std::uint8_t* const header = pageNumber == 1 ? bytes + format::DatabaseHeader::SIZE : bytes;
    const format::btree::PageHeader view(header);
    if (!view.HasValidType() || !view.HasValidCellCount(m_pageSize)) {
      level.m_page.Reset();
      return ResultCode::Corrupt;
    }
    level.m_bytes = bytes;
    level.m_header = header;
    level.m_cellCount = view.GetCellCount();
    level.m_index = 0;
    level.m_isLeaf = view.IsLeaf();
    return ResultCode::Ok;
  }

  [[nodiscard]] ResultCode TryMoveToChild(std::uint32_t pageNumber) noexcept {
    if (m_depth + 1 >= MAX_DEPTH) {
      return ResultCode::Corrupt;
    }
    Level& child = m_levels[m_depth + 1];
    const ResultCode loaded = TryLoad(child, pageNumber);
    if (loaded != ResultCode::Ok) {
      return loaded;
    }
    if (child.m_cellCount == 0 || !format::btree::PageHeader(child.m_header).IsTable()) {
      child.m_page.Reset();
      return ResultCode::Corrupt;
    }
    ++m_depth;
    return ResultCode::Ok;
  }

  [[nodiscard]] ResultCode TryMoveToLeftmost() noexcept {
    while (!m_levels[m_depth].m_isLeaf) {
      const ResultCode moved = TryMoveToChild(format::btree::TableInteriorCell(GetCell(m_levels[m_depth])).GetLeftChild());
      if (moved != ResultCode::Ok) {
        return moved;
      }
    }
    ParseCell();
    return ResultCode::Ok;
  }

  void ParseCell() noexcept {
    const Level& level = m_levels[m_depth];
    const format::btree::TableLeafCell cell(GetCell(level), m_usableSize);
    m_rowid = cell.GetRowid();
    m_payload = cell.GetPayload();
    m_payloadSize = cell.GetPayloadSize();
    m_localPayloadSize = cell.GetLocalPayloadSize();
    m_reader.SetCell(level.m_bytes, m_payload, m_payloadSize, m_localPayloadSize);
  }

  [[nodiscard]] const std::uint8_t* GetCell(const Level& level) const noexcept {
    return level.m_bytes + (format::btree::PageHeader(level.m_header).GetCellOffset(level.m_index) & (m_pageSize - 1));
  }

  static constexpr std::size_t MAX_DEPTH = 20;

  pager::PageCache* m_cache = nullptr;
  PayloadReader m_reader;
  std::uint32_t m_pageSize = 0;
  std::uint32_t m_usableSize = 0;
  std::uint32_t m_pageCount = 0;
  std::uint32_t m_root = 0;
  std::array<Level, MAX_DEPTH> m_levels;
  std::size_t m_depth = 0;
  std::int64_t m_rowid = 0;
  const std::uint8_t* m_payload = nullptr;
  std::uint32_t m_payloadSize = 0;
  std::uint32_t m_localPayloadSize = 0;
};

}  // namespace aiodrivers::sqlite::btree