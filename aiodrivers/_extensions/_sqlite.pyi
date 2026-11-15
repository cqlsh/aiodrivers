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

from os import PathLike
from typing import Self, final

from _typeshed import structseq

@final
class DatabaseHeader(structseq[int], tuple[int, ...]):
    """The fields of the 100-byte header at the start of a database file.

    :attr:`page_count` comes back as stored. SQLite only trusts it if it is
    nonzero and :attr:`version_valid_for` equals :attr:`change_counter`.
    """

    @property
    def page_size(self) -> int:
        """:class:`int`: The page size in bytes."""

    @property
    def write_version(self) -> int:
        """:class:`int`: The write version, 1 for rollback journal mode and 2 for WAL."""

    @property
    def read_version(self) -> int:
        """:class:`int`: The read version, 1 for rollback journal mode and 2 for WAL."""

    @property
    def reserved_space(self) -> int:
        """:class:`int`: The number of bytes reserved at the end of each page."""

    @property
    def usable_size(self) -> int:
        """:class:`int`: The usable bytes per page, :attr:`page_size` minus :attr:`reserved_space`."""

    @property
    def change_counter(self) -> int:
        """:class:`int`: The file change counter."""

    @property
    def page_count(self) -> int:
        """:class:`int`: The database size in pages, as stored in the header."""

    @property
    def first_freelist_page(self) -> int:
        """:class:`int`: The first freelist trunk page, or 0 if the freelist is empty."""

    @property
    def freelist_page_count(self) -> int:
        """:class:`int`: The total number of freelist pages."""

    @property
    def schema_cookie(self) -> int:
        """:class:`int`: The schema cookie, incremented on every schema change."""

    @property
    def schema_format(self) -> int:
        """:class:`int`: The schema format number."""

    @property
    def default_cache_size(self) -> int:
        """:class:`int`: The suggested page cache size, or 0 if none was set."""

    @property
    def largest_root_page(self) -> int:
        """:class:`int`: The largest root page when auto-vacuum is on, otherwise 0."""

    @property
    def text_encoding(self) -> int:
        """:class:`int`: The text encoding, 1 for UTF-8, 2 for UTF-16le, 3 for UTF-16be and 0 in a file without tables."""

    @property
    def user_version(self) -> int:
        """:class:`int`: The value set with ``PRAGMA user_version``."""

    @property
    def incremental_vacuum(self) -> bool:
        """:class:`bool`: Whether incremental-vacuum mode is on."""

    @property
    def application_id(self) -> int:
        """:class:`int`: The value set with ``PRAGMA application_id``."""

    @property
    def version_valid_for(self) -> int:
        """:class:`int`: The change counter at the time :attr:`page_count` and :attr:`library_version` were last written."""

    @property
    def library_version(self) -> int:
        """:class:`int`: The version number of the SQLite library that last wrote the file, e.g. 3049001 for 3.49.1."""

@final
class Database:
    """A read-only handle to a database file.

    The path is turned into a full path the way SQLite does it, and the header
    is read once while opening. Errors are raised as the classes from
    :mod:`aiodrivers.exceptions`, with SQLite's result code in ``code``.
    Leaving a ``with`` block closes the database.

    Parameters
    -----------
    path: Union[:class:`str`, :class:`bytes`, :class:`os.PathLike`]
        The path of the database file.

    Raises
    -------
    OperationalError
        The file can't be opened or read.
    """

    def __new__(cls, path: str | bytes | PathLike[str] | PathLike[bytes], /) -> Self: ...

    @property
    def path(self) -> str:
        """:class:`str`: The full path of the database file."""

    def read_header(self) -> DatabaseHeader:
        """Reads the header again and checks it the way SQLite does before the first statement.

        Returns
        --------
        :class:`DatabaseHeader`
            The fields of the header.

        Raises
        -------
        DatabaseError
            The file is not a database or is corrupt.
        OperationalError
            Reading the file failed.
        InterfaceError
            The database is closed.
        """

    def close(self) -> None:
        """Closes the database. Calling it again does nothing."""

    def __enter__(self) -> Self: ...

    def __exit__(self, *args: object) -> None: ...

__all__ = ('Database', 'DatabaseHeader')