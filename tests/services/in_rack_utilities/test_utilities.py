# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-30A: rack::string, rack::random and rack::logger inside Rack."""

import itertools
import time

import pytest

from rackpy.raw import logger, macros, settings
from rackpy.raw import random as rr
from rackpy.raw import string as rs


def test_printf_through_racks_formatting() -> None:
    assert rs.f("%d apples and %s", 3, "pears") == "3 apples and pears"
    assert rs.f("%5.2f|%-4d|%x|%o|%c|%%", 3.14159, 7, 255, 8, "A") == " 3.14|7   |ff|10|A|%"
    assert rs.f("%lld %llu %hhd %hu", -(2**62), 2**64 - 1, -128, 65535) == f"{-(2**62)} {2**64 - 1} -128 65535"
    assert rs.f("%e %g %a", 1500.0, 0.0001, 1.0) == "1.500000e+03 0.0001 0x1p+0"
    assert rs.fv("%s-%s", ["a", "b"]) == "a-b" and rs.f("no conversions") == "no conversions"
    with pytest.raises(TypeError, match="more arguments"):
        rs.f("%d %d", 1)
    with pytest.raises(TypeError, match="takes 1"):
        rs.f("%d", 1, 2)
    with pytest.raises(TypeError, match="needs an int"):
        rs.f("%d", "x")
    with pytest.raises(ValueError, match="cannot represent"):
        rs.f("%d", 2**40)  # an int is 32 bits
    with pytest.raises(ValueError, match="not supported"):
        rs.f("%n", 1)
    with pytest.raises(ValueError, match=r"\* widths"):
        rs.f("%*d", 3, 1)
    with pytest.raises(ValueError, match="NUL"):
        rs.f("%s", "a\0b")


def test_text() -> None:
    assert rs.lowercase("ÀBC") == "Àbc" and rs.uppercase("abc") == "ABC"  # Rack changes ASCII only
    assert rs.trim("  x \n") == "x"
    assert rs.truncate("héllo", 3) == "hél" and rs.truncate_prefix("héllo", 2) == "lo"
    assert rs.ellipsize("abcdef", 4) == "abc…" and rs.ellipsize_prefix("abcdef", 4) == "…def"
    assert rs.starts_with("rackpy", "rack") and rs.ends_with("rackpy", "py") and not rs.starts_with("a", "ab")
    location = rs.position_to_location("ab\ncd", 4)
    assert (location.line, location.column) == (1, 1) and rs.location_to_position("ab\ncd", location) == 4
    assert rs.to_base64(b"RackPy") == "UmFja1B5" and rs.from_base64("UmFja1B5") == b"RackPy"
    assert rs.from_base64("Um Fj\na1B5!") == b"RackPy"  # Rack 2.6.6 skips non-Base64 characters (it never raises)
    assert rs.strcasecmp("Rack", "rACK") == 0 and rs.strcasecmp("a", "b") < 0
    less = rs.CaseInsensitiveCompare()
    assert less("apple", "Banana") and not less("Banana", "apple")
    assert rs.join(["a", "b", "c"], "+") == "a+b+c" and rs.join(["x"]) == "x"
    assert rs.split("a+b+c", "+") == ["a", "b", "c"] and rs.split("a++c", "+") == ["a", "", "c"]
    assert rs.split("", "+") == [] and rs.split("a+b+c", "+", 2) == ["a", "b+c"]
    with pytest.raises(RuntimeError):
        rs.split("abc", "")  # Rack's Exception


def test_time_and_unicode() -> None:
    assert rs.format_time("%Y", 0).isdigit()
    assert len(rs.format_time_iso(time.time())) >= 19
    with pytest.raises(ValueError, match="range"):
        rs.format_time("%Y", float("nan"))
    with pytest.raises(ValueError):
        rs.format_time("%Y", 1e300)  # Rack would pass strftime a null time
    assert rs.utf8_to_utf32("aé€😀") == [0x61, 0xE9, 0x20AC, 0x1F600]
    assert rs.utf32_to_utf8([0x61, 0x1F600, 0x110000]) == "a😀".encode()  # Rack skips codepoints past U+10FFFF
    with pytest.raises(ValueError, match="never returns"):
        rs.utf8_to_utf32(b"a\xffb")  # Rack 2.6.6 would loop forever on invalid UTF-8
    assert rs.utf8_to_utf32(b"\xc3\xa9") == [0xE9] and rs.utf8_next_codepoint(b"a\xffb", 1) == 1  # invalid: no step
    text = "aé€"
    assert rs.utf8_length(text) == 3 and rs.utf8_next_codepoint(text, 1) == 3 and rs.utf8_prev_codepoint(text, 3) == 1
    assert rs.utf8_codepoint_index(text, 3) == 2 and rs.utf8_codepoint_pos(text, 2) == 3


def test_versions_and_translations() -> None:
    order = ["1.a.0", "1.b.0", "1.0.0", "1.0.1", "1.2.0", "1.10.0", "2.0.0"]  # Rack's documented order
    versions = [rs.Version(v) for v in order]
    assert all(a < b for a, b in itertools.pairwise(versions))
    v = rs.Version("2.6.6")
    assert (
        v.parts == ["2", "6", "6"]
        and str(v) == "2.6.6"
        and (v.get_major(), v.get_minor(), v.get_revision())
        == (
            "2",
            "6",
            "6",
        )
    )
    assert rs.Version().parts == []
    languages = rs.get_languages()
    assert "en" in languages
    assert rs.translate("language", "en") and rs.translate("no.such.id.rackpy", "en") == ""
    saved_language = settings.settings.language
    try:
        settings.settings.language = "rackpy-no-such-language"
        assert rs.translate("language") == rs.translate("language", "en") != ""
    finally:
        settings.settings.language = saved_language
    rs.init()  # reloads the translations
    assert rs.get_languages() == languages


def test_random() -> None:
    a, b = rr.Xoroshiro128Plus(42, 7), rr.Xoroshiro128Plus(42, 7)
    assert [a() for _ in range(5)] == [b() for _ in range(5)]  # deterministic from a seed
    g = rr.Xoroshiro128Plus()
    assert not g.is_seeded() and g() == 0 and g.state == [0, 0]
    g.seed(1)
    assert g.is_seeded() and len(g.state) == 2
    g.state = [1, 2]
    assert g() == 3  # s0 + s1
    assert rr.Xoroshiro128Plus.rotl(1, 63) == 2**63 and rr.Xoroshiro128Plus.min() == 0
    assert rr.Xoroshiro128Plus.max() == 2**64 - 1 and rr.Xoroshiro128Plus.result_type is int
    with pytest.raises(ValueError, match="1 to 63"):
        rr.Xoroshiro128Plus.rotl(1, 0)
    with pytest.raises(ValueError):
        g.state = [1]
    shared = rr.local()
    assert shared is rr.local() or shared.state == rr.local().state  # Rack's one generator
    for _ in range(20):
        assert 0.0 <= rr.uniform() < 1.0 and 0 <= rr.get_uint8() <= 255 and 0 <= rr.u32() < 2**32
        assert 0.0 <= rr.get_float() < 1.0 and 0.0 <= rr.get_double() < 1.0
        assert 0 <= rr.get_uint16() < 2**16 and 0 <= rr.get_uint32() < 2**32
        assert isinstance(rr.get_bool(), bool) and 0 <= rr.u64() < 2**64
    assert isinstance(rr.normal(), float) and isinstance(rr.get("bool"), bool) and 0 <= rr.get() < 2**64
    with pytest.raises(ValueError, match="takes"):
        rr.get("int")
    out = bytearray(10)
    rr.buffer(out)
    assert len(rr.vector(7)) == 7
    with pytest.raises((TypeError, BufferError)):
        rr.buffer(b"read-only")  # type: ignore[arg-type]
    rr.init()  # reseeds Rack's generator


def test_random_get_signed_types() -> None:
    # Rack's primary get<T> converts the next 64-bit number to T; seeded draws are compared with Rack's own in
    # tests/values/in_rack/test_reference_replay.py.
    generator = rr.local()
    saved = generator.state
    try:
        for type, bits in (("int64", 64), ("int32", 32), ("int16", 16), ("int8", 8)):
            generator.seed(5, 9)
            expected = rr.Xoroshiro128Plus(5, 9)()  # the same next number, unconverted
            value = rr.get(type)
            assert isinstance(value, int) and -(2 ** (bits - 1)) <= value < 2 ** (bits - 1)
            assert value % 2**bits == expected % 2**bits  # the low bits, as C++'s conversion keeps them
    finally:
        generator.state = saved


def test_logging_macro_levels(monkeypatch: pytest.MonkeyPatch) -> None:
    calls: list[tuple[object, ...]] = []
    monkeypatch.setattr(logger, "log", lambda *args: calls.append(args))
    for macro in (macros.DEBUG, macros.INFO, macros.WARN, macros.FATAL):
        macro("level %d", 7)
    assert [call[0] for call in calls] == [
        logger.Level.DEBUG_LEVEL,
        logger.Level.INFO_LEVEL,
        logger.Level.WARN_LEVEL,
        logger.Level.FATAL_LEVEL,
    ]
    assert all(
        str(call[1]).endswith("test_utilities.py") and call[3:] == ("test_logging_macro_levels", "level %d", 7)
        for call in calls
    )


def test_logging() -> None:
    logger.log(logger.Level.INFO_LEVEL, "rackpy_test.py", 12, "test_logging", "rackpy-log %s %d", "marker", 5)
    macros.DEBUG("rackpy-macro %s", "debugged")
    macros.INFO("rackpy-macro %s", "informed")
    macros.WARN("rackpy-macro %s", "warned")
    macros.FATAL("rackpy-macro %s", "fatal")
    assert isinstance(logger.get_log_path(), str)
    with pytest.raises(TypeError):
        logger.log(logger.Level.INFO_LEVEL, "f", 1, "g", "%d")
