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

#include "sqlite/pager/page.hpp"
#include "sqlite/result_code.hpp"
#include "sqlite/vfs/file.hpp"

#include <cstdint>
#include <memory>
#include <new>
#include <utility>

namespace aiodrivers::sqlite::pager {

/**
 * @brief Keeps recently used pages of one database file in memory, like SQLite's page cache.
 * @details A fetched page stays pinned while a Reference holds it, and only unpinned pages get recycled, least recently used
 *          first. The capacity is a soft limit: when every page is pinned, the cache grows past it and shrinks back as pages
 *          are released, which is what SQLite's cache does too. Not synchronized, and References must not outlive the cache.
 */
class PageCache {
  struct Entry;

 public:
  /** @brief Keeps one page of the cache pinned until the reference goes away or is reset. */
  class Reference {
   public:
    /** @brief Creates a reference that holds no page. */
    Reference() noexcept = default;

    ~Reference() noexcept { Reset(); }

    Reference(const Reference&) = delete;
    Reference& operator=(const Reference&) = delete;

    /**
     * @brief Unpins the current page and takes over the page of other.
     * @param other Source; it holds no page afterwards.
     * @return This object.
     */
    Reference& operator=(Reference&& other) noexcept {
      if (&other != this) {
        Reset();
        m_cache = std::exchange(other.m_cache, nullptr);
        m_entry = std::exchange(other.m_entry, nullptr);
      }
      return *this;
    }

    /** @brief Unpins the page; does nothing if the reference holds none. */
    void Reset() noexcept {
      if (m_entry != nullptr) {
        m_cache->Release(m_entry);
        m_entry = nullptr;
        m_cache = nullptr;
      }
    }

    /**
     * @brief Returns the pinned page.
     * @pre The reference holds a page.
     */
    [[nodiscard]] const Page& GetPage() const noexcept { return m_entry->m_page; }

   private:
    friend class PageCache;

    Reference(PageCache& cache, Entry& entry) noexcept : m_cache(&cache), m_entry(&entry) {}

    PageCache* m_cache = nullptr;
    Entry* m_entry = nullptr;
  };

  /**
   * @brief Creates an empty cache.
   * @param file The database file; must outlive the cache.
   * @param pageSize Page size of the database in bytes.
   * @param capacity Number of pages the cache keeps once they are released; pinned pages can exceed it.
   */
  PageCache(const vfs::File& file, std::uint32_t pageSize, std::uint32_t capacity) noexcept
      : m_file(&file),
        m_pageSize(pageSize),
        m_capacity(capacity) {}

  ~PageCache() noexcept {
    for (std::uint32_t bucket = 0; bucket < m_bucketCount; ++bucket) {
      Entry* entry = m_buckets[bucket];
      while (entry != nullptr) {
        Entry* const next = entry->m_nextInBucket;
        delete entry;
        entry = next;
      }
    }
  }

  PageCache(const PageCache&) = delete;
  PageCache& operator=(const PageCache&) = delete;
  PageCache(PageCache&&) = delete;
  PageCache& operator=(PageCache&&) = delete;

  /**
   * @brief Hands out a page and reads it from the file only if the cache doesn't hold it yet.
   * @details A read that fails leaves nothing behind in the cache, as in SQLite. Checking pageNumber against the page count
   *          is up to the caller.
   * @pre pageSize is a power of two from 512 to 65536.
   * @param pageNumber Number of the page, starting at 1.
   * @param[out] output Reference that pins the page. It is reset first, so it holds no page on failure.
   * @retval Ok output holds the page.
   * @retval Corrupt pageNumber is 0.
   * @retval NoMem There was no memory for another page.
   * @retval IoErrRead Reading the file failed.
   * @retval IoErrCorruptFs Reading failed with an error that points to a damaged disk or file system (POSIX only).
   */
  [[nodiscard]] ResultCode TryFetch(std::uint32_t pageNumber, Reference& output) noexcept {
    output.Reset();
    if (pageNumber == 0) {
      return ResultCode::Corrupt;
    }
    Entry* entry = Find(pageNumber);
    if (entry == nullptr) {
      entry = TryTakeEntry();
      if (entry == nullptr) {
        return ResultCode::NoMem;
      }
      const ResultCode read = entry->m_page.TryRead(*m_file, pageNumber, m_pageSize);
      if (read != ResultCode::Ok) {
        delete entry;
        --m_pageCount;
        return read;
      }
      entry->m_number = pageNumber;
      Insert(*entry);
    } else if (entry->m_pins == 0) {
      Unlink(*entry);
    }
    ++entry->m_pins;
    output = Reference(*this, *entry);
    return ResultCode::Ok;
  }

 private:
  struct Entry {
    Page m_page;
    std::uint32_t m_number = 0;
    std::uint32_t m_pins = 0;
    Entry* m_nextInBucket = nullptr;
    Entry* m_newer = nullptr;
    Entry* m_older = nullptr;
  };

  [[nodiscard]] Entry* Find(std::uint32_t pageNumber) const noexcept {
    if (m_bucketCount == 0) {
      return nullptr;
    }
    Entry* entry = m_buckets[pageNumber & (m_bucketCount - 1)];
    while (entry != nullptr && entry->m_number != pageNumber) {
      entry = entry->m_nextInBucket;
    }
    return entry;
  }

  [[nodiscard]] Entry* TryTakeEntry() noexcept {
    if (m_pageCount >= m_capacity && m_oldest != nullptr) {
      Entry* const victim = m_oldest;
      Unlink(*victim);
      Remove(*victim);
      return victim;
    }
    if (!TryGrowBuckets()) {
      return nullptr;
    }
    auto* const entry = new (std::nothrow) Entry();
    if (entry != nullptr) {
      ++m_pageCount;
    }
    return entry;
  }

  [[nodiscard]] bool TryGrowBuckets() noexcept {
    if (m_pageCount < m_bucketCount) {
      return true;
    }
    const std::uint32_t size = m_bucketCount == 0 ? MIN_BUCKET_COUNT : m_bucketCount * 2;
    std::unique_ptr<Entry*[]> buckets(new (std::nothrow) Entry*[size]());
    if (buckets == nullptr) {
      return m_bucketCount != 0;
    }
    for (std::uint32_t bucket = 0; bucket < m_bucketCount; ++bucket) {
      Entry* entry = m_buckets[bucket];
      while (entry != nullptr) {
        Entry* const next = entry->m_nextInBucket;
        Entry*& head = buckets[entry->m_number & (size - 1)];
        entry->m_nextInBucket = head;
        head = entry;
        entry = next;
      }
    }
    m_buckets = std::move(buckets);
    m_bucketCount = size;
    return true;
  }

  void Insert(Entry& entry) noexcept {
    Entry*& head = m_buckets[entry.m_number & (m_bucketCount - 1)];
    entry.m_nextInBucket = head;
    head = &entry;
  }

  void Remove(Entry& entry) noexcept {
    Entry** link = &m_buckets[entry.m_number & (m_bucketCount - 1)];
    while (*link != &entry) {
      link = &(*link)->m_nextInBucket;
    }
    *link = entry.m_nextInBucket;
    entry.m_nextInBucket = nullptr;
  }

  void Unlink(Entry& entry) noexcept {
    if (entry.m_newer != nullptr) {
      entry.m_newer->m_older = entry.m_older;
    } else {
      m_newest = entry.m_older;
    }
    if (entry.m_older != nullptr) {
      entry.m_older->m_newer = entry.m_newer;
    } else {
      m_oldest = entry.m_newer;
    }
    entry.m_newer = nullptr;
    entry.m_older = nullptr;
  }

  void Release(Entry* entry) noexcept {
    if (--entry->m_pins != 0) {
      return;
    }
    if (m_pageCount > m_capacity) {
      Remove(*entry);
      delete entry;
      --m_pageCount;
      return;
    }
    entry->m_older = m_newest;
    if (m_newest != nullptr) {
      m_newest->m_newer = entry;
    } else {
      m_oldest = entry;
    }
    m_newest = entry;
  }

  static constexpr std::uint32_t MIN_BUCKET_COUNT = 64;

  const vfs::File* m_file = nullptr;
  std::uint32_t m_pageSize = 0;
  std::uint32_t m_capacity = 0;
  std::uint32_t m_pageCount = 0;
  std::unique_ptr<Entry*[]> m_buckets;
  std::uint32_t m_bucketCount = 0;
  Entry* m_newest = nullptr;
  Entry* m_oldest = nullptr;
};

}  // namespace aiodrivers::sqlite::pager