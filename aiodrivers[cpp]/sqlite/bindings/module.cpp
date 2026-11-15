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

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sqlite/bindings/module_state.hpp"
#include "sqlite/bindings/types/database_header_type.hpp"
#include "sqlite/bindings/types/database_type.hpp"

#include <new>

namespace aiodrivers::sqlite::bindings {

/**
 * @brief Defines the extension module aiodrivers._extensions._sqlite.
 * @details Uses multi-phase initialization and doesn't support subinterpreters, so importing it in a second interpreter of
 *          the same process raises ImportError.
 */
class Module {
 public:
  Module() = delete;

  /** @brief Returns the module definition. It is static and lives as long as the process. */
  [[nodiscard]] static PyModuleDef* GetDefinition() noexcept {
    static PyModuleDef_Slot slots[] = {
        {Py_mod_exec, reinterpret_cast<void*>(&Module::Execute)},
        {Py_mod_multiple_interpreters, Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED},
        {0, nullptr}
    };
    static PyModuleDef definition = {
        .m_base = PyModuleDef_HEAD_INIT,
        .m_name = "aiodrivers._extensions._sqlite",
        .m_doc = nullptr,
        .m_size = static_cast<Py_ssize_t>(sizeof(ModuleState)),
        .m_methods = nullptr,
        .m_slots = slots,
        .m_traverse = &Module::Traverse,
        .m_clear = &Module::Clear,
        .m_free = &Module::Free
    };
    return &definition;
  }

 private:
  [[nodiscard]] static int Execute(PyObject* module) noexcept {
    auto* const state = new (PyModule_GetState(module)) ModuleState();
    if (!state->TryImportExceptions()) {
      return -1;
    }
    PyObject* const headerType = types::DatabaseHeaderType::TryCreate();
    if (headerType == nullptr) {
      return -1;
    }
    state->SetHeaderType(headerType);
    if (PyModule_AddObjectRef(module, "DatabaseHeader", headerType) != 0) {
      return -1;
    }
    PyObject* const databaseType = types::DatabaseType::TryCreate(module);
    if (databaseType == nullptr) {
      return -1;
    }
    const int added = PyModule_AddObjectRef(module, "Database", databaseType);
    Py_DECREF(databaseType);
    return added;
  }

  [[nodiscard]] static int Traverse(PyObject* module, visitproc visit, void* argument) noexcept {
    return ModuleState::Of(module)->Traverse(visit, argument);
  }

  [[nodiscard]] static int Clear(PyObject* module) noexcept {
    ModuleState::Of(module)->Clear();
    return 0;
  }

  static void Free(void* module) noexcept { ModuleState::Of(static_cast<PyObject*>(module))->Clear(); }
};

}  // namespace aiodrivers::sqlite::bindings

/** @brief Hands Python the module definition when it imports aiodrivers._extensions._sqlite. */
PyMODINIT_FUNC PyInit__sqlite() {
  return PyModuleDef_Init(aiodrivers::sqlite::bindings::Module::GetDefinition());
}