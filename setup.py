"""
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
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

from setuptools import Extension, setup

class NativeModules:
    """Tells setuptools how to build the C++ extension modules.

    Each module is compiled from every ``.cpp`` file in its folders below
    ``aiodrivers[cpp]``, so new source files are picked up without touching
    this file. Set ``AIODRIVERS_NO_EXTENSIONS=1`` to skip the C++ build.

    Attributes
    -----------
    FOLDERS: Dict[:class:`str`, Tuple[:class:`str`, ...]]
        The folders below ``aiodrivers[cpp]`` that make up each module.
    """

    __slots__ = ()

    FOLDERS: dict[str, tuple[str, ...]] = {'_sqlite': ('core', 'sqlite')}

    @staticmethod
    def flags() -> list[str]:
        """Returns the compiler flags for the current platform.

        Returns
        --------
        List[:class:`str`]
            MSVC flags on Windows, GCC or Clang flags everywhere else.
        """
        if sys.platform == 'win32':
            return ['/std:c++20', '/EHsc', '/W4', '/permissive-', '/utf-8']

        return ['-std=c++20', '-Wall', '-Wextra', '-fvisibility=hidden']

    @staticmethod
    def files(folders: tuple[str, ...], pattern: str) -> list[str]:
        """Collects the files of a module.

        Parameters
        -----------
        folders: Tuple[:class:`str`, ...]
            The folders below ``aiodrivers[cpp]`` to search, including subfolders.
        pattern: :class:`str`
            A glob pattern for the file names, e.g. ``'*.cpp'``.

        Returns
        --------
        List[:class:`str`]
            The matching paths, sorted and relative to the project root.
        """
        return sorted(path.as_posix() for folder in folders for path in Path('aiodrivers[cpp]', folder).rglob(pattern))

    @classmethod
    def extension(cls, name: str, folders: tuple[str, ...]) -> Extension:
        """Builds the extension description for one module.

        Parameters
        -----------
        name: :class:`str`
            The module name inside ``aiodrivers._extensions``.
        folders: Tuple[:class:`str`, ...]
            The folders below ``aiodrivers[cpp]`` that hold its sources.

        Returns
        --------
        :class:`setuptools.Extension`
            The extension setuptools compiles.
        """
        return Extension(
            f'aiodrivers._extensions.{name}',
            cls.files(folders, '*.cpp'),
            depends=cls.files(folders, '*.hpp'),
            include_dirs=['aiodrivers[cpp]'],
            extra_compile_args=cls.flags(),
            language='c++'
        )

    @classmethod
    def extensions(cls) -> list[Extension]:
        """Describes every module that has source files.

        Returns
        --------
        List[:class:`setuptools.Extension`]
            The modules to build. Empty if ``AIODRIVERS_NO_EXTENSIONS`` is
            ``1`` or there are no source files yet.
        """
        if os.environ.get('AIODRIVERS_NO_EXTENSIONS') == '1':
            return []

        return [cls.extension(name, folders) for name, folders in cls.FOLDERS.items() if cls.files(folders, '*.cpp')]

setup(ext_modules=NativeModules.extensions())