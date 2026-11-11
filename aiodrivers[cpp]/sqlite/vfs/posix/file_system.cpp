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

#if !defined(_WIN32)

#include "sqlite/vfs/file_system.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace aiodrivers::sqlite::vfs {

namespace {

/**
 * @brief Builds a full path one name at a time and resolves symbolic links on the way, like SQLite's unixFullPathname.
 * @details While building, the path may grow to 511 bytes, since a later ".." can shorten it again. At most 201 links are
 *          followed, and each target must be shorter than FILENAME_MAX bytes. Names that don't exist stay in the path; any
 *          other failed lookup stops the build. Targets of nested links wait in a 4096-byte buffer inside the builder until
 *          their names are appended, and only targets beyond that go to the heap.
 */
class PathBuilder {
 public:
  /**
   * @brief Appends the slash-separated names of path to the result.
   * @details Handles "." and ".." and follows symbolic links. A leading slash doesn't reset the result.
   * @param path Zero-terminated path.
   */
  void Append(const char* path) noexcept {
    std::size_t start = 0;
    std::size_t end = 0;
    while (!m_failed) {
      while (path[end] != '\0' && path[end] != '/') {
        ++end;
      }
      if (end > start) {
        AppendName(path + start, end - start);
      }
      if (path[end] == '\0') {
        return;
      }
      start = ++end;
    }
  }

  /**
   * @brief Copies the finished path into output.
   * @param[out] output The full path, zero-terminated; unchanged on failure.
   * @retval Ok The path was copied.
   * @retval IoErrNoMem Allocating memory for a link target failed.
   * @retval CantOpen A lookup failed, or the path is just "/" or longer than MAX_FULL_PATH_SIZE_IN_BYTES.
   */
  [[nodiscard]] ResultCode TryCopyTo(FileSystem::FullPath& output) const noexcept {
    if (m_outOfMemory) {
      return ResultCode::IoErrNoMem;
    }
    if (m_failed || m_sizeInBytes < 2 || m_sizeInBytes > FileSystem::MAX_FULL_PATH_SIZE_IN_BYTES) {
      return ResultCode::CantOpen;
    }
    std::memcpy(output.data(), m_bytes.data(), m_sizeInBytes);
    output[m_sizeInBytes] = '\0';
    return ResultCode::Ok;
  }

 private:
  void AppendName(const char* name, std::size_t sizeInBytes) noexcept {
    if (sizeInBytes == 1 && name[0] == '.') {
      return;
    }
    if (sizeInBytes == 2 && name[0] == '.' && name[1] == '.') {
      RemoveLastName();
      return;
    }
    if (m_sizeInBytes + 1 + sizeInBytes >= MAX_SIZE_IN_BYTES) {
      m_failed = true;
      return;
    }
    m_bytes[m_sizeInBytes] = '/';
    std::memcpy(m_bytes.data() + m_sizeInBytes + 1, name, sizeInBytes);
    m_sizeInBytes += 1 + sizeInBytes;
    m_bytes[m_sizeInBytes] = '\0';
    struct stat status = {};
    if (::lstat(m_bytes.data(), &status) != 0) {
      if (errno != ENOENT) {
        m_failed = true;
      }
      return;
    }
    if (S_ISLNK(status.st_mode)) {
      FollowLink(sizeInBytes);
    }
  }

  void FollowLink(std::size_t nameSizeInBytes) noexcept {
    if (m_linkCount >= MAX_LINK_COUNT) {
      m_failed = true;
      return;
    }
    ++m_linkCount;
    const ssize_t readInBytes = ::readlink(m_bytes.data(), m_linkTarget.data(), m_linkTarget.size());
    if (readInBytes <= 0 || static_cast<std::size_t>(readInBytes) >= m_linkTarget.size()) {
      m_failed = true;
      return;
    }
    const auto targetSizeInBytes = static_cast<std::size_t>(readInBytes);
    const bool fits = m_targetsSizeInBytes + targetSizeInBytes < m_targets.size();
    std::unique_ptr<char[]> allocated;
    if (!fits) {
      allocated.reset(new (std::nothrow) char[targetSizeInBytes + 1]);
      if (allocated == nullptr) {
        m_failed = true;
        m_outOfMemory = true;
        return;
      }
    }
    char* const target = fits ? m_targets.data() + m_targetsSizeInBytes : allocated.get();
    std::memcpy(target, m_linkTarget.data(), targetSizeInBytes);
    target[targetSizeInBytes] = '\0';
    const std::size_t reservedInBytes = fits ? targetSizeInBytes + 1 : 0;
    m_targetsSizeInBytes += reservedInBytes;
    m_sizeInBytes = target[0] == '/' ? 0 : m_sizeInBytes - 1 - nameSizeInBytes;
    Append(target);
    m_targetsSizeInBytes -= reservedInBytes;
  }

  void RemoveLastName() noexcept {
    if (m_sizeInBytes > 1) {
      while (m_bytes[--m_sizeInBytes] != '/') {}
    }
  }

  static constexpr std::size_t MAX_SIZE_IN_BYTES = 512;
  static constexpr std::size_t TARGET_SPACE_IN_BYTES = 4096;
  static constexpr int MAX_LINK_COUNT = 201;

  std::array<char, MAX_SIZE_IN_BYTES> m_bytes;
  std::array<char, FILENAME_MAX> m_linkTarget;
  std::array<char, TARGET_SPACE_IN_BYTES> m_targets;
  std::size_t m_sizeInBytes = 0;
  std::size_t m_targetsSizeInBytes = 0;
  int m_linkCount = 0;
  bool m_failed = false;
  bool m_outOfMemory = false;
};

}  // namespace

ResultCode FileSystem::TryGetFullPath(const char* path, FullPath& output) noexcept {
  PathBuilder builder;
  if (path[0] != '/') {
    std::array<char, FILENAME_MAX> workingFolder;
    if (::getcwd(workingFolder.data(), workingFolder.size()) == nullptr) {
      return ResultCode::CantOpen;
    }
    builder.Append(workingFolder.data());
  }
  builder.Append(path);
  return builder.TryCopyTo(output);
}

}  // namespace aiodrivers::sqlite::vfs

#endif