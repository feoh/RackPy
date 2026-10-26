# SPDX-License-Identifier: GPL-3.0-or-later
"""rackpy.errors: every public error type, its built-in base and its documentation."""

import inspect

import pytest

from rackpy import errors

pytestmark = pytest.mark.unit

BUILTIN_BASES = {
    "StaleHandleError": ReferenceError,
    "ContextError": RuntimeError,
    "ThreadError": RuntimeError,
    "OwnershipError": ValueError,
    "EngineLockError": RuntimeError,
    "LaboratoryError": RuntimeError,
}


def test_public_errors_are_exactly_the_documented_set() -> None:
    public = {name for name, obj in vars(errors).items() if isinstance(obj, type) and issubclass(obj, BaseException)}
    assert public == {*BUILTIN_BASES, "RackPyError"} == set(errors.__all__)


@pytest.mark.parametrize(("name", "base"), sorted(BUILTIN_BASES.items()))
def test_each_error_is_a_rackpy_error_and_the_expected_builtin(name: str, base: type[Exception]) -> None:
    cls = getattr(errors, name)
    assert issubclass(cls, errors.RackPyError) and issubclass(cls, base)
    with pytest.raises(base):
        raise cls("example")
    with pytest.raises(errors.RackPyError):
        raise cls("example")


@pytest.mark.parametrize("name", [*BUILTIN_BASES, "RackPyError"])
def test_each_error_is_documented(name: str) -> None:
    doc = inspect.getdoc(getattr(errors, name)) or ""
    assert len(doc.split()) >= 5, name


def test_native_translation_covers_every_error() -> None:
    """native/runtime/python.cpp maps one C++ exception to each error; keep the two lists in step."""
    from pathlib import Path

    source = (Path(__file__).resolve().parents[2] / "native" / "runtime" / "python.cpp").read_text()
    translated = set(__import__("re").findall(r'errorType\("(\w+)"\)', source))
    assert translated == set(BUILTIN_BASES)
