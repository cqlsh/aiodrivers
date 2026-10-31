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
        .m_size = 0,
        .m_methods = nullptr,
        .m_slots = slots,
        .m_traverse = nullptr,
        .m_clear = nullptr,
        .m_free = nullptr
    };
    return &definition;
  }

 private:
  [[nodiscard]] static int Execute(PyObject*) noexcept { return 0; }
};

}  // namespace aiodrivers::sqlite::bindings

/** @brief Hands Python the module definition when it imports aiodrivers._extensions._sqlite. */
PyMODINIT_FUNC PyInit__sqlite() {
  return PyModuleDef_Init(aiodrivers::sqlite::bindings::Module::GetDefinition());
}