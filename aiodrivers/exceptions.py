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

class DriverError(Exception):
    """Base exception class for aiodrivers.

    Catch this to handle any error a driver can raise.
    """

class InterfaceError(DriverError):
    """Raised when the library is used the wrong way.

    For example, when a query is sent over a connection that is already closed.
    """

class ProtocolError(DriverError):
    """Raised when the database sends data the driver can't make sense of.

    That means either a bug in the driver or a server that speaks a protocol
    the driver doesn't expect.
    """

class DatabaseError(DriverError):
    """Raised when the database reports an error.

    Attributes
    -----------
    code: Optional[Union[:class:`int`, :class:`str`]]
        The error code the database sent, or ``None`` if it didn't send one.
    """

    __slots__ = ['code']

    def __init__(self, message: str, *, code: int | str | None = None) -> None:
        super().__init__(message)
        self.code: int | str | None = code

class DataError(DatabaseError):
    """Raised when the database can't process a value.

    A number that is out of range or a division by zero are typical causes.
    """

class OperationalError(DatabaseError):
    """Raised when the database fails for a reason that has nothing to do with the query itself.

    A lost connection or a server that is shutting down, for example.
    """

class IntegrityError(DatabaseError):
    """Raised when a statement violates a constraint, such as a duplicate key or a failed foreign key check."""

class InternalError(DatabaseError):
    """Raised when the database runs into an internal error.

    This covers things like an invalid cursor or a transaction that is out of sync.
    """

class ProgrammingError(DatabaseError):
    """Raised when the statement itself is wrong, e.g. a syntax error or a table that doesn't exist."""

class NotSupportedError(DatabaseError):
    """Raised when the database doesn't support the requested operation."""

__all__ = (
    'DriverError',
    'InterfaceError',
    'ProtocolError',
    'DatabaseError',
    'DataError',
    'OperationalError',
    'IntegrityError',
    'InternalError',
    'ProgrammingError',
    'NotSupportedError'
)