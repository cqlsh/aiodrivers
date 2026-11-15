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

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sqlite/database.hpp"
#include "sqlite/format/database_header.hpp"

#include <array>
#include <cstddef>

namespace aiodrivers::sqlite::bindings::types {

/** @brief Defines DatabaseHeader, a struct sequence (named tuple) with the fields of the database header. */
class DatabaseHeaderType {
 public:
  DatabaseHeaderType() = delete;

  /**
   * @brief Creates the DatabaseHeader type.
   * @return New reference to the type, or nullptr with an exception set.
   */
  [[nodiscard]] static PyObject* TryCreate() noexcept {
    static auto fields = [] {
      std::array<PyStructSequence_Field, FIELDS.size() + 1> result = {};
      for (std::size_t index = 0; index < FIELDS.size(); ++index) {
        result[index] = {.name = FIELDS[index].m_name, .doc = FIELDS[index].m_documentation};
      }
      return result;
    }();
    static PyStructSequence_Desc description = {
        .name = "aiodrivers._extensions._sqlite.DatabaseHeader",
        .doc = "The fields of the 100-byte header at the start of a database file.",
        .fields = fields.data(),
        .n_in_sequence = static_cast<int>(FIELDS.size())
    };
    return reinterpret_cast<PyObject*>(PyStructSequence_NewType(&description));
  }

  /**
   * @brief Creates a DatabaseHeader from the raw header bytes.
   * @param type The DatabaseHeader type from the module state.
   * @param bytes The 100 header bytes.
   * @return New reference to the value, or nullptr with an exception set.
   */
  [[nodiscard]] static PyObject* TryCreateValue(PyTypeObject* type, const Database::Header& bytes) noexcept {
    PyObject* const value = PyStructSequence_New(type);
    if (value == nullptr) {
      return nullptr;
    }
    const format::DatabaseHeader header(bytes.data());
    for (std::size_t index = 0; index < FIELDS.size(); ++index) {
      PyObject* const item = FIELDS[index].m_read(header);
      if (item == nullptr) {
        Py_DECREF(value);
        return nullptr;
      }
      PyStructSequence_SetItem(value, static_cast<Py_ssize_t>(index), item);
    }
    return value;
  }

 private:
  struct Field {
    const char* m_name;
    const char* m_documentation;
    PyObject* (*m_read)(const format::DatabaseHeader&);
  };

  static constexpr std::array<Field, 19> FIELDS = {{
      {"page_size", "Page size in bytes.",
          [](const format::DatabaseHeader& header) { return PyLong_FromUnsignedLong(header.GetPageSize()); }},
      {"write_version", "Write version: 1 for rollback journal mode, 2 for WAL.",
          [](const format::DatabaseHeader& header) { return PyLong_FromUnsignedLong(header.GetWriteVersion()); }},
      {"read_version", "Read version: 1 for rollback journal mode, 2 for WAL.",
          [](const format::DatabaseHeader& header) { return PyLong_FromUnsignedLong(header.GetReadVersion()); }},
      {"reserved_space", "Number of bytes reserved at the end of each page.",
          [](const format::DatabaseHeader& header) { return PyLong_FromUnsignedLong(header.GetReservedSpace()); }},
      {"usable_size", "Usable bytes per page: the page size minus the reserved space.",
          [](const format::DatabaseHeader& header) { return PyLong_FromUnsignedLong(header.GetUsableSize()); }},
      {"change_counter", "File change counter.",
          [](const format::DatabaseHeader& header) { return PyLong_FromUnsignedLong(header.GetChangeCounter()); }},
      {"page_count", "Database size in pages, as stored in the header.",
          [](const format::DatabaseHeader& header) { return PyLong_FromUnsignedLong(header.GetPageCount()); }},
      {"first_freelist_page", "First freelist trunk page, or 0 if the freelist is empty.",
          [](const format::DatabaseHeader& header) { return PyLong_FromUnsignedLong(header.GetFirstFreelistPage()); }},
      {"freelist_page_count", "Total number of freelist pages.",
          [](const format::DatabaseHeader& header) { return PyLong_FromUnsignedLong(header.GetFreelistPageCount()); }},
      {"schema_cookie", "Schema cookie, incremented on every schema change.",
          [](const format::DatabaseHeader& header) { return PyLong_FromUnsignedLong(header.GetSchemaCookie()); }},
      {"schema_format", "Schema format number.",
          [](const format::DatabaseHeader& header) { return PyLong_FromUnsignedLong(header.GetSchemaFormat()); }},
      {"default_cache_size", "Suggested page cache size, or 0 if none was set.",
          [](const format::DatabaseHeader& header) { return PyLong_FromUnsignedLong(header.GetDefaultCacheSize()); }},
      {"largest_root_page", "Largest root page when auto-vacuum is on, otherwise 0.",
          [](const format::DatabaseHeader& header) { return PyLong_FromUnsignedLong(header.GetLargestRootPage()); }},
      {"text_encoding", "Text encoding: 1 for UTF-8, 2 for UTF-16le, 3 for UTF-16be, 0 in a file without tables.",
          [](const format::DatabaseHeader& header) { return PyLong_FromUnsignedLong(header.GetTextEncoding()); }},
      {"user_version", "Value set with PRAGMA user_version.",
          [](const format::DatabaseHeader& header) { return PyLong_FromLong(header.GetUserVersion()); }},
      {"incremental_vacuum", "Whether incremental-vacuum mode is on.",
          [](const format::DatabaseHeader& header) { return PyBool_FromLong(header.IsIncrementalVacuum() ? 1 : 0); }},
      {"application_id", "Value set with PRAGMA application_id.",
          [](const format::DatabaseHeader& header) { return PyLong_FromLong(header.GetApplicationId()); }},
      {"version_valid_for", "Change counter at the time the page count and the library version were last written.",
          [](const format::DatabaseHeader& header) { return PyLong_FromUnsignedLong(header.GetVersionValidFor()); }},
      {"library_version", "Version number of the SQLite library that last wrote the file, e.g. 3049001 for 3.49.1.",
          [](const format::DatabaseHeader& header) { return PyLong_FromUnsignedLong(header.GetLibraryVersion()); }}
  }};
};

}  // namespace aiodrivers::sqlite::bindings::types