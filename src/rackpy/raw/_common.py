# SPDX-License-Identifier: GPL-3.0-or-later
"""common.hpp mappings that are plain Python: rackpy.raw.Exception, construct, get and DeferWrapper.

Each one maps a C++ construct whose meaning Python can express directly
(docs/reference/values.md explains each choice). They are part of
rackpy.raw; import them from there.
"""

import builtins
import re
from collections.abc import Callable, Mapping, Sequence
from types import TracebackType
from typing import Any

__all__ = ["DeferWrapper", "Exception", "construct", "defer_wrapper", "get"]

# printf length modifiers that Python's % operator does not accept.
_LENGTH_MODIFIERS = re.compile(r"%([-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?)(?:hh|h|ll|l|L|q|j|z|t)?([diouxXeEfFgGcsa%])")


class Exception(builtins.RuntimeError):  # mirrors rack::Exception by name
    """rack::Exception: an error Rack or a plugin throws on purpose.

    Rack's C++ exceptions arrive in Python as this type. ``msg`` and
    ``what()`` are the message, as in C++.
    """

    msg: str

    def __init__(self, msg: str = "") -> None:
        super().__init__(msg)
        self.msg = msg

    @classmethod
    def printf(cls, format: str, *args: object) -> Exception:
        """The ``Exception(const char* format, ...)`` constructor: printf-style formatting.

        Formats with Python's ``%`` operator after removing C length modifiers
        (``%lld`` becomes ``%d``), so the common printf conversions behave as in C.
        """
        return cls(_LENGTH_MODIFIERS.sub(r"%\1\2", format) % args)

    def what(self) -> str:
        """The message, as ``std::exception::what()`` returns it."""
        return self.msg

    def __str__(self) -> str:
        return self.msg


def construct[T](cls: type[T], **fields: Any) -> T:
    """``construct<T>(&T::field, value, ...)``: a new ``cls()`` with the given fields set.

    The C++ helper returns a raw pointer the caller owns; in Python the new
    object is simply yours.
    """
    obj = cls()
    for name, value in fields.items():
        setattr(obj, name, value)
    return obj


_MISSING = object()


def get(container: Mapping[Any, Any] | Sequence[Any], key: Any, default: Any = None) -> Any:
    """``rack::get``: a value from a mapping by key, or from a sequence by index, else ``default``.

    Like the C++ helpers it never inserts the key. A sequence index outside
    ``0 <= key < len`` (including a negative one, which C++ sees as a huge
    unsigned index) gives ``default``. C++ defaults to a value-initialized
    ``T()``; Python has no type to construct one from, so the default is ``None``.
    """
    if isinstance(container, Mapping):
        return container.get(key, default)
    if 0 <= key < len(container):
        return container[key]
    return default


class DeferWrapper:
    """``rack::DeferWrapper<F>``: runs ``f`` when the scope ends.

    C++ runs ``f`` in the wrapper's destructor. Python has no deterministic
    destructors, so the scope is a ``with`` block: ``f`` runs when the block
    exits, normally or by an exception.
    """

    f: Callable[[], object]
    """The function to run (DeferWrapper::f)."""

    def __init__(self, f: Callable[[], object]) -> None:
        self.f = f

    def __enter__(self) -> DeferWrapper:
        return self

    def __exit__(
        self, exc_type: type[BaseException] | None, exc: BaseException | None, tb: TracebackType | None
    ) -> None:
        self.f()


def defer_wrapper(f: Callable[[], object]) -> DeferWrapper:
    """``rack::deferWrapper(f)``: the DeferWrapper for ``f``."""
    return DeferWrapper(f)
