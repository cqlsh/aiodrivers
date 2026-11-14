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

#include "sqlite/result_code.hpp"
#include "sqlite/result_message.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace aiodrivers::sqlite::bindings {

/**
 * @brief Holds the objects shared by the types of the extension module.
 * @details Python allocates the state together with the module and zero-fills it; Module::Execute constructs and fills it.
 *          The garbage collector reaches the objects through Traverse.
 */
class ModuleState {
 public:
  /**
   * @brief Returns the state of a module object.
   * @param module The extension module.
   * @return Its state, owned by the module.
   */
  [[nodiscard]] static ModuleState* Of(PyObject* module) noexcept { return static_cast<ModuleState*>(PyModule_GetState(module)); }

  /**
   * @brief Returns the state of the module that created a type.
   * @param type A heap type created by the extension module.
   * @return The state of that module.
   */
  [[nodiscard]] static ModuleState* Of(PyTypeObject* type) noexcept { return Of(PyType_GetModule(type)); }

  /**
   * @brief Imports aiodrivers.exceptions and keeps references to the classes RaiseError needs.
   * @return False with a Python exception set if the import or a lookup fails.
   */
  [[nodiscard]] bool TryImportExceptions() noexcept {
    static constexpr std::array<std::pair<const char*, PyObject* ModuleState::*>, 6> CLASSES = {{
        {"InterfaceError", &ModuleState::m_interfaceError},
        {"DatabaseError", &ModuleState::m_databaseError},
        {"DataError", &ModuleState::m_dataError},
        {"OperationalError", &ModuleState::m_operationalError},
        {"IntegrityError", &ModuleState::m_integrityError},
        {"InternalError", &ModuleState::m_internalError}
    }};
    PyObject* const exceptions = PyImport_ImportModule("aiodrivers.exceptions");
    if (exceptions == nullptr) {
      return false;
    }
    for (const auto& [name, member] : CLASSES) {
      this->*member = PyObject_GetAttrString(exceptions, name);
      if (this->*member == nullptr) {
        Py_DECREF(exceptions);
        return false;
      }
    }
    Py_DECREF(exceptions);
    m_codeKeyword = Py_BuildValue("(s)", "code");
    return m_codeKeyword != nullptr;
  }

  /**
   * @brief Stores the DatabaseHeader type.
   * @param type New reference to the type; the state takes it over.
   */
  void SetHeaderType(PyObject* type) noexcept { m_headerType = type; }

  /**
   * @brief Raises the exception Python's sqlite3 module would raise for a result code.
   * @details The class depends on the primary code, as in sqlite3. The message is SQLite's text for the code, and the code
   *          attribute holds the full extended code. NoMem and IoErrNoMem raise MemoryError, and InterfaceError is raised
   *          without a code because it doesn't take one.
   * @param code Result code of the failed call.
   * @return Always nullptr, so callers can return it directly.
   */
  [[nodiscard]] PyObject* RaiseError(ResultCode code) noexcept {
    const auto value = static_cast<std::int32_t>(code);
    const auto primary = static_cast<ResultCode>(value & 0xFF);
    if (primary == ResultCode::NoMem || code == ResultCode::IoErrNoMem) {
      return PyErr_NoMemory();
    }
    PyObject* const type = GetErrorClass(primary);
    PyObject* const message = PyUnicode_FromString(ResultMessage::Get(code));
    if (message == nullptr) {
      return nullptr;
    }
    if (type == m_interfaceError) {
      PyErr_SetObject(type, message);
      Py_DECREF(message);
      return nullptr;
    }
    PyObject* const number = PyLong_FromLong(value);
    if (number == nullptr) {
      Py_DECREF(message);
      return nullptr;
    }
    const std::array<PyObject*, 2> arguments = {message, number};
    PyObject* const error = PyObject_Vectorcall(type, arguments.data(), 1, m_codeKeyword);
    Py_DECREF(number);
    Py_DECREF(message);
    if (error != nullptr) {
      PyErr_SetObject(type, error);
      Py_DECREF(error);
    }
    return nullptr;
  }

  /**
   * @brief Raises InterfaceError.
   * @param message Error message.
   * @return Always nullptr.
   */
  [[nodiscard]] PyObject* RaiseInterfaceError(const char* message) noexcept {
    PyErr_SetString(m_interfaceError, message);
    return nullptr;
  }

  /** @brief Returns the DatabaseHeader type as a borrowed reference. */
  [[nodiscard]] PyTypeObject* GetHeaderType() const noexcept { return reinterpret_cast<PyTypeObject*>(m_headerType); }

  /**
   * @brief Visits the objects of the state for the garbage collector.
   * @param visit Visitor passed in by the collector.
   * @param argument Argument to pass on to visit.
   * @return 0, or the first nonzero result of visit.
   */
  [[nodiscard]] int Traverse(visitproc visit, void* argument) noexcept {
    for (PyObject* ModuleState::* const member : GetMembers()) {
      PyObject* const object = this->*member;
      if (object != nullptr) {
        const int result = visit(object, argument);
        if (result != 0) {
          return result;
        }
      }
    }
    return 0;
  }

  /** @brief Releases all objects of the state. Calling it again is safe. */
  void Clear() noexcept {
    for (PyObject* ModuleState::* const member : GetMembers()) {
      Py_CLEAR(this->*member);
    }
  }

 private:
  [[nodiscard]] static constexpr std::array<PyObject* ModuleState::*, 8> GetMembers() noexcept {
    return {
        &ModuleState::m_headerType,
        &ModuleState::m_codeKeyword,
        &ModuleState::m_interfaceError,
        &ModuleState::m_databaseError,
        &ModuleState::m_dataError,
        &ModuleState::m_operationalError,
        &ModuleState::m_integrityError,
        &ModuleState::m_internalError
    };
  }

  [[nodiscard]] PyObject* GetErrorClass(ResultCode primary) const noexcept {
    switch (primary) {
      case ResultCode::Internal:
      case ResultCode::NotFound:
        return m_internalError;
      case ResultCode::Error:
      case ResultCode::Perm:
      case ResultCode::Abort:
      case ResultCode::Busy:
      case ResultCode::Locked:
      case ResultCode::ReadOnly:
      case ResultCode::Interrupt:
      case ResultCode::IoErr:
      case ResultCode::Full:
      case ResultCode::CantOpen:
      case ResultCode::Protocol:
      case ResultCode::Empty:
      case ResultCode::Schema:
        return m_operationalError;
      case ResultCode::TooBig:
        return m_dataError;
      case ResultCode::Constraint:
      case ResultCode::Mismatch:
        return m_integrityError;
      case ResultCode::Misuse:
      case ResultCode::Range:
        return m_interfaceError;
      default:
        return m_databaseError;
    }
  }

  PyObject* m_headerType = nullptr;
  PyObject* m_codeKeyword = nullptr;
  PyObject* m_interfaceError = nullptr;
  PyObject* m_databaseError = nullptr;
  PyObject* m_dataError = nullptr;
  PyObject* m_operationalError = nullptr;
  PyObject* m_integrityError = nullptr;
  PyObject* m_internalError = nullptr;
};

}  // namespace aiodrivers::sqlite::bindings