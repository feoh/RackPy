# SPDX-License-Identifier: GPL-3.0-or-later
"""common.hpp and arch.hpp inside Rack: configuration strings, bitCast, literals, Exception,
construct, get, DeferWrapper and the macros."""

import struct
import warnings

import pytest

from rackpy import raw
from rackpy.raw import literals, macros
from rackpy.raw.math import Rect, Vec

try:
    from _rackpy_host import _testing
except ImportError:
    _testing = None  # type: ignore[assignment]

hooks = pytest.mark.skipif(_testing is None, reason="needs the test-hooks build (the production run covers the rest)")


def test_app_configuration_strings_are_the_running_racks() -> None:
    assert raw.APP_VERSION == "2.6.6" and raw.APP_VERSION_MAJOR == "2"
    assert raw.APP_NAME == "VCV Rack" and raw.APP_OS == "lin" and raw.APP_CPU == "x64"
    for name in ("APP_EDITION", "APP_EDITION_NAME", "APP_OS_NAME", "APP_CPU_NAME", "API_URL"):
        assert isinstance(getattr(raw, name), str) and getattr(raw, name)


def test_bit_cast_reinterprets_bits_of_the_same_size() -> None:
    assert raw.bit_cast(1.0, "u32", "f32") == 0x3F800000
    assert raw.bit_cast(0x3F800000, "f32", "u32") == 1.0
    assert raw.bit_cast(-1, "u64", "i64") == 2**64 - 1
    assert raw.bit_cast(2**63, "i64", "u64") == -(2**63)
    assert raw.bit_cast(-0.0, "u32", "f32") == 0x80000000
    nan_bits = raw.bit_cast(float("nan"), "u64", "f64")
    assert nan_bits == struct.unpack("<Q", struct.pack("<d", float("nan")))[0]
    assert raw.bit_cast(255, "i8", "u8") == -1


def test_bit_cast_refuses_what_rack_would_not_compile() -> None:
    with pytest.raises(ValueError, match="differ in size"):
        raw.bit_cast(1.0, "u64", "f32")
    with pytest.raises(ValueError, match="type must be one of"):
        raw.bit_cast(1, "u128", "u64")
    with pytest.raises(OverflowError):
        raw.bit_cast(256, "i8", "u8")
    with pytest.raises(TypeError):
        raw.bit_cast(1.5, "f32", "i32")


def test_user_defined_literals_convert_as_cpp_does() -> None:
    assert literals.u8(255) == 255 and literals.u8(256) == 0 and literals.i8(200) == -56
    assert literals.i16(70000) == 4464 and literals.u16(65536) == 0
    assert literals.i32(2**31) == -(2**31) and literals.u32(2**32 + 5) == 5
    assert literals.i64(2**63) == -(2**63) and literals.u64(2**64 - 1) == 2**64 - 1
    assert literals.f32(0.1) == 0.10000000149011612 and literals.f32(3) == 3.0
    assert literals.f64(0.1) == 0.1 and literals.f64(2**53 + 1) == 2.0**53
    with pytest.raises(OverflowError):
        literals.u8(-1)  # a C++ literal is never negative
    with pytest.raises(OverflowError):
        literals.u64(2**64)


def test_exception_carries_its_message() -> None:
    e = raw.Exception("bad patch")
    assert e.msg == "bad patch" and e.what() == "bad patch" and str(e) == "bad patch"
    assert isinstance(e, RuntimeError)
    p = raw.Exception.printf("module %lld has %d ports: %s", 42, 3, "ok")
    assert p.msg == "module 42 has 3 ports: ok"
    with pytest.raises(raw.Exception, match="bad patch"):
        raise e


@hooks
def test_rack_exceptions_arrive_as_rackpy_raw_exception() -> None:
    assert _testing is not None
    with pytest.raises(raw.Exception, match="thrown by Rack") as info:
        _testing.throw_rack_exception("thrown by Rack")
    assert info.value.msg == "thrown by Rack"


def test_construct_sets_fields_on_a_new_object() -> None:
    r = raw.construct(Rect, pos=Vec(1, 2), size=Vec(3, 4))
    assert r == Rect(1, 2, 3, 4)
    assert raw.construct(Vec) == Vec()


def test_get_reads_mappings_by_key_and_sequences_by_index() -> None:
    table = {"a": 1}
    assert raw.get(table, "a") == 1 and raw.get(table, "b", 3) == 3 and raw.get(table, "b") is None
    assert "b" not in table  # like the C++ helper, it never inserts
    values = [10, 20]
    assert raw.get(values, 1) == 20 and raw.get(values, 2, -1) == -1 and raw.get(values, -1, -1) == -1


def test_defer_runs_at_the_end_of_the_block_even_on_error() -> None:
    ran: list[str] = []
    with raw.defer_wrapper(lambda: ran.append("wrapper")) as d:
        assert isinstance(d, raw.DeferWrapper) and ran == []
    with pytest.raises(KeyError), macros.DEFER(lambda: ran.append("macro")):
        raise KeyError
    with raw.DeferWrapper(lambda: ran.append("class")):
        pass
    assert ran == ["wrapper", "macro", "class"]


def test_arch_macros_are_the_built_platform() -> None:
    assert macros.ARCH_LIN == 1 and macros.ARCH_X64 == 1  # type: ignore[attr-defined]
    for absent in ("ARCH_WIN", "ARCH_MAC", "ARCH_ARM64"):
        assert not hasattr(macros, absent)


def test_value_macros() -> None:
    assert macros.VEC_ARGS(Vec(1, 2)) == (1.0, 2.0)
    assert macros.RECT_ARGS(Rect(1, 2, 3, 4)) == (1.0, 2.0, 3.0, 4.0)
    assert macros.LENGTHOF([1, 2, 3]) == 3
    assert macros.ENUMS("BAR", 14, start=1) == {"BAR": 1, "BAR_LAST": 14}


def test_deprecated_marks_and_warns() -> None:
    @macros.DEPRECATED
    def old() -> int:
        return 1

    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        assert old() == 1
    assert caught and issubclass(caught[0].category, DeprecationWarning)
