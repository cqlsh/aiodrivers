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

#include "sqlite/bindings/module_state.hpp"
#include "sqlite/bindings/types/database_header_type.hpp"
#include "sqlite/database.hpp"
#include "sqlite/result_code.hpp"

#include <mutex>
#include <new>

namespace aiodrivers::sqlite::bindings::types {

/**
 * @brief Defines the Python type Database, a read-only handle to a database file.
 * @details File work runs with the GIL released. A mutex keeps close() from cutting into a read, so the methods are safe to
 *          call from several threads.
 */
class DatabaseType {
 public:
  DatabaseType() = delete;

  /**
   * @brief Creates the Database type.
   * @param module The extension module; instances reach the module state through it.
   * @return New reference to the type, or nullptr with an exception set.
   */
  [[nodiscard]] static PyObject* TryCreate(PyObject* module) noexcept {
    static PyMethodDef methods[] = {
        {"read_header", &DatabaseType::ReadHeader, METH_NOARGS,
            "read_header($self, /)\n--\n\nReads the header again and checks it the way SQLite does before the first statement."},
        {"close", &DatabaseType::Close, METH_NOARGS, "close($self, /)\n--\n\nCloses the database. Calling it again does nothing."},
        {"__enter__", &DatabaseType::Enter, METH_NOARGS, "__enter__($self, /)\n--\n\nReturns the database."},
        {"__exit__", &DatabaseType::Exit, METH_VARARGS, "__exit__($self, *args, /)\n--\n\nCloses the database."},
        {nullptr, nullptr, 0, nullptr}
    };
    static PyGetSetDef properties[] = {
        {"path", &DatabaseType::GetPath, nullptr, "Full path of the database file.", nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr}
    };
    static PyType_Slot slots[] = {
        {Py_tp_doc, const_cast<char*>("Database(path, /)\n--\n\nA read-only handle to the database file at path.")},
        {Py_tp_new, reinterpret_cast<void*>(&DatabaseType::New)},
        {Py_tp_dealloc, reinterpret_cast<void*>(&DatabaseType::Dealloc)},
        {Py_tp_methods, methods},
        {Py_tp_getset, properties},
        {0, nullptr}
    };
    static PyType_Spec specification = {
        .name = "aiodrivers._extensions._sqlite.Database",
        .basicsize = static_cast<int>(sizeof(Object)),
        .itemsize = 0,
        .flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_IMMUTABLETYPE,
        .slots = slots
    };
    return PyType_FromModuleAndSpec(module, &specification, nullptr);
  }

 private:
  struct Object {
    PyObject_HEAD
    Database m_database;
    std::mutex m_mutex;
  };

  [[nodiscard]] static PyObject* New(PyTypeObject* type, PyObject* arguments, PyObject* keywords) noexcept {
    ModuleState* const state = ModuleState::Of(type);
    if (keywords != nullptr && PyDict_GET_SIZE(keywords) != 0) {
      PyErr_SetString(PyExc_TypeError, "Database() takes no keyword arguments");
      return nullptr;
    }
    PyObject* path = nullptr;
    if (PyArg_ParseTuple(arguments, "O&:Database", PyUnicode_FSConverter, &path) == 0) {
      return nullptr;
    }
    PyObject* const self = type->tp_alloc(type, 0);
    if (self == nullptr) {
      Py_DECREF(path);
      return nullptr;
    }
    auto* const object = reinterpret_cast<Object*>(self);
    new (&object->m_database) Database();
    new (&object->m_mutex) std::mutex();
    ResultCode code = ResultCode::Ok;
    Py_BEGIN_ALLOW_THREADS
    code = object->m_database.TryOpen(PyBytes_AS_STRING(path));
    Py_END_ALLOW_THREADS
    Py_DECREF(path);
    if (code != ResultCode::Ok) {
      Py_DECREF(self);
      return state->RaiseError(code);
    }
    return self;
  }

  static void Dealloc(PyObject* self) noexcept {
    auto* const object = reinterpret_cast<Object*>(self);
    PyTypeObject* const type = Py_TYPE(self);
    object->m_mutex.~mutex();
    object->m_database.~Database();
    type->tp_free(self);
    Py_DECREF(type);
  }

  [[nodiscard]] static PyObject* ReadHeader(PyObject* self, PyObject*) noexcept {
    auto* const object = reinterpret_cast<Object*>(self);
    ModuleState* const state = ModuleState::Of(Py_TYPE(self));
    Database::Header header = {};
    bool open = false;
    ResultCode code = ResultCode::Ok;
    Py_BEGIN_ALLOW_THREADS
    {
      const std::lock_guard<std::mutex> lock(object->m_mutex);
      open = object->m_database.IsOpen();
      if (open) {
        code = object->m_database.TryReadHeader(header);
      }
    }
    Py_END_ALLOW_THREADS
    if (!open) {
      return state->RaiseInterfaceError("Cannot operate on a closed database.");
    }
    if (code != ResultCode::Ok) {
      return state->RaiseError(code);
    }
    return DatabaseHeaderType::TryCreateValue(state->GetHeaderType(), header);
  }

  [[nodiscard]] static PyObject* Close(PyObject* self, PyObject*) noexcept {
    auto* const object = reinterpret_cast<Object*>(self);
    Py_BEGIN_ALLOW_THREADS
    {
      const std::lock_guard<std::mutex> lock(object->m_mutex);
      object->m_database.Close();
    }
    Py_END_ALLOW_THREADS
    Py_RETURN_NONE;
  }

  [[nodiscard]] static PyObject* Enter(PyObject* self, PyObject*) noexcept { return Py_NewRef(self); }

  [[nodiscard]] static PyObject* Exit(PyObject* self, PyObject*) noexcept { return Close(self, nullptr); }

  [[nodiscard]] static PyObject* GetPath(PyObject* self, void*) noexcept {
    return PyUnicode_DecodeFSDefault(reinterpret_cast<Object*>(self)->m_database.GetPath());
  }
};

}  // namespace aiodrivers::sqlite::bindings::types