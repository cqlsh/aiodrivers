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
#include "sqlite/pager/page.hpp"
#include "sqlite/result_code.hpp"
#include "sqlite/vfs/file.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace aiodrivers::sqlite::btree {

/**
 * @brief Reads the payload of a cell: the part on the page and the overflow chain behind it.
 * @details One reader belongs to one cursor, and SetCell points it at the next cell. It remembers the page numbers of the
 *          chain per cell, so reading the columns of a row one by one doesn't walk the chain from the start every time.
 *          Not synchronized.
 */
class PayloadReader {
 public:
  /**
   * @brief Creates a reader for the overflow chains of one database.
   * @param file The database file; must outlive the reader.
   * @param pageSize Page size in bytes.
   * @param usableSize Usable bytes per page, the page size minus the reserved space.
   * @param pageCount Size of the database in pages, as SQLite's b-tree layer counts it.
   */
  PayloadReader(const vfs::File& file, std::uint32_t pageSize, std::uint32_t usableSize, std::uint32_t pageCount) noexcept
      : m_file(&file),
        m_pageSize(pageSize),
        m_usableSize(usableSize),
        m_pageCount(pageCount) {}

  PayloadReader(const PayloadReader&) = delete;
  PayloadReader& operator=(const PayloadReader&) = delete;

  /**
   * @brief Points the reader at the payload of a cell and forgets the chain of the previous one.
   * @param page Start of the page that holds the cell; it must stay loaded while the cell is read.
   * @param payload Start of the payload inside the page.
   * @param payloadSize Size of the whole payload in bytes.
   * @param localSize Payload bytes the cell keeps on the page, as the cell states it (not cut off at the end of the page).
   */
  void SetCell(const std::uint8_t* page, const std::uint8_t* payload, std::uint32_t payloadSize, std::uint32_t localSize) noexcept {
    m_page = page;
    m_payload = payload;
    m_payloadSize = payloadSize;
    m_localSize = localSize;
    m_isChainKnown = false;
  }

  /**
   * @brief Copies amount bytes of the payload, starting at offset, into output.
   * @details Works like SQLite's accessPayload, error paths included; a chain that loops back just repeats its content there
   *          too. An overflow page needed from its first byte, after at least 4 bytes of output, is read straight from the
   *          file into output and only as far as needed, like SQLite's direct overflow read. If the file ends before that,
   *          the result is IoErrShortRead. Other overflow pages go through a page buffer, where a short read gives zeros, as
   *          in SQLite's pager. Autovacuum pointer maps aren't used to skip pages yet.
   * @pre SetCell was called, and offset + amount doesn't exceed the payload size.
   * @param offset Position in the payload to start at.
   * @param amount Number of bytes to copy.
   * @param[out] output Destination with room for amount bytes.
   * @retval Ok All bytes were copied.
   * @retval Corrupt The local part runs past the usable area of the page, a page number in the chain is larger than the
   *         page count, or the chain ends before the payload does.
   * @retval NoMem The list of chain pages couldn't grow.
   * @retval IoErrShortRead The file ends inside an overflow page that was read directly.
   * @retval IoErrRead Reading an overflow page failed.
   * @retval IoErrCorruptFs Reading failed with an error that points to a damaged disk or file system (POSIX only).
   */
  [[nodiscard]] ResultCode TryRead(std::uint32_t offset, std::uint32_t amount, std::uint8_t* output) noexcept {
    if (static_cast<std::size_t>(m_payload - m_page) > m_usableSize - m_localSize) {
      return ResultCode::Corrupt;
    }
    const std::uint8_t* const start = output;
    if (offset < m_localSize) {
      const std::uint32_t chunk = std::min(amount, m_localSize - offset);
      std::memcpy(output, m_payload + offset, chunk);
      offset = 0;
      output += chunk;
      amount -= chunk;
    } else {
      offset -= m_localSize;
    }
    if (amount == 0) {
      return ResultCode::Ok;
    }
    const std::uint32_t chunkSize = m_usableSize - NEXT_PAGE_SIZE;
    std::uint32_t next = core::bytes::BigEndian::ReadUInt32(m_payload + m_localSize);
    std::uint32_t index = 0;
    if (!m_isChainKnown) {
      if (!TryStartChain(chunkSize)) {
        return ResultCode::NoMem;
      }
    } else if (m_chain[offset / chunkSize] != 0) {
      index = offset / chunkSize;
      next = m_chain[index];
      offset %= chunkSize;
    }
    while (next != 0) {
      if (next > m_pageCount) {
        return ResultCode::Corrupt;
      }
      m_chain[index] = next;
      ResultCode result = ResultCode::Ok;
      if (offset >= chunkSize) {
        result = TryGetNextPage(index, next, next);
        offset -= chunkSize;
      } else {
        const std::uint32_t chunk = std::min(amount, chunkSize - offset);
        if (offset == 0 && static_cast<std::size_t>(output - start) >= NEXT_PAGE_SIZE) {
          result = TryReadDirectly(next, chunk, output, next);
        } else {
          result = TryReadThroughPage(next, offset, chunk, output, next);
        }
        offset = 0;
        amount -= chunk;
        if (amount == 0) {
          return result;
        }
        output += chunk;
      }
      if (result != ResultCode::Ok) {
        return result;
      }
      ++index;
    }
    return ResultCode::Corrupt;
  }

 private:
  [[nodiscard]] bool TryStartChain(std::uint32_t chunkSize) noexcept {
    const auto count = static_cast<std::uint32_t>((static_cast<std::uint64_t>(m_payloadSize) - m_localSize + chunkSize - 1) / chunkSize) + 1;
    if (count > m_chainCapacity) {
      std::unique_ptr<std::uint32_t[]> chain(new (std::nothrow) std::uint32_t[std::size_t{count} * 2]);
      if (chain == nullptr) {
        return false;
      }
      m_chain = std::move(chain);
      m_chainCapacity = count * 2;
    }
    std::memset(m_chain.get(), 0, std::size_t{count} * sizeof(std::uint32_t));
    m_isChainKnown = true;
    return true;
  }

  [[nodiscard]] ResultCode TryGetNextPage(std::uint32_t index, std::uint32_t pageNumber, std::uint32_t& outNext) noexcept {
    if (m_chain[index + 1] != 0) {
      outNext = m_chain[index + 1];
      return ResultCode::Ok;
    }
    const ResultCode result = m_overflowPage.TryRead(*m_file, pageNumber, m_pageSize);
    outNext = result == ResultCode::Ok ? core::bytes::BigEndian::ReadUInt32(m_overflowPage.GetBytes()) : 0;
    return result;
  }

  [[nodiscard]] ResultCode TryReadDirectly(std::uint32_t pageNumber, std::uint32_t chunk, std::uint8_t* output, std::uint32_t& outNext) noexcept {
    std::uint8_t* const target = output - NEXT_PAGE_SIZE;
    std::array<std::uint8_t, NEXT_PAGE_SIZE> saved;
    std::memcpy(saved.data(), target, saved.size());
    const ResultCode result = m_file->TryRead(target, chunk + NEXT_PAGE_SIZE, static_cast<std::uint64_t>(pageNumber - 1) * m_pageSize);
    outNext = core::bytes::BigEndian::ReadUInt32(target);
    std::memcpy(target, saved.data(), saved.size());
    return result;
  }

  [[nodiscard]] ResultCode TryReadThroughPage(
      std::uint32_t pageNumber, std::uint32_t offset, std::uint32_t chunk, std::uint8_t* output, std::uint32_t& outNext
  ) noexcept {
    const ResultCode result = m_overflowPage.TryRead(*m_file, pageNumber, m_pageSize);
    if (result == ResultCode::Ok) {
      outNext = core::bytes::BigEndian::ReadUInt32(m_overflowPage.GetBytes());
      std::memcpy(output, m_overflowPage.GetBytes() + NEXT_PAGE_SIZE + offset, chunk);
    }
    return result;
  }

  static constexpr std::uint32_t NEXT_PAGE_SIZE = 4;

  const vfs::File* m_file = nullptr;
  std::uint32_t m_pageSize = 0;
  std::uint32_t m_usableSize = 0;
  std::uint32_t m_pageCount = 0;
  const std::uint8_t* m_page = nullptr;
  const std::uint8_t* m_payload = nullptr;
  std::uint32_t m_payloadSize = 0;
  std::uint32_t m_localSize = 0;
  bool m_isChainKnown = false;
  std::unique_ptr<std::uint32_t[]> m_chain;
  std::uint32_t m_chainCapacity = 0;
  pager::Page m_overflowPage;
};

}  // namespace aiodrivers::sqlite::btree