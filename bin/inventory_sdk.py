#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12"
# dependencies = ["cyclopts>=5.2,<6", "clang==21.1.7"]
#
# [tool.uv]
# exclude-newer = "2026-10-21T00:00:00Z"
# ///
"""Exhaustive AST and preprocessor inventory of the public Rack SDK surface (RP-05).

Parses a translation unit that includes only rack.hpp with libclang, records
every declaration and macro in Rack's own headers, and writes
upstream/api-surface.json. The schema is docs/reference/surface-schema.md.

This is an inventory, not a coverage report: it says what exists, never what
RackPy has bound. Extraction errors abort without writing anything.
"""

from __future__ import annotations

import ctypes
import hashlib
import json
import re
import shutil
import subprocess
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Annotated, Any

import cyclopts
from clang import cindex as ci  # type: ignore[import-untyped]  # the bindings ship no stubs

REPO = Path(__file__).resolve().parent.parent
OUTPUT = REPO / "upstream" / "api-surface.json"
SUMMARY = REPO / "docs" / "reference" / "surface-summary.md"
VERSIONS = REPO / "upstream" / "versions.json"
SCHEMA_VERSION = 1
BINDINGS = "clang==21.1.7"
PRIVATE_MESSAGE = "Using internal Rack function or symbol"
K = ci.CursorKind

FUNCTION_KINDS = {
    K.FUNCTION_DECL: "function",
    K.CXX_METHOD: "method",
    K.CONSTRUCTOR: "constructor",
    K.DESTRUCTOR: "destructor",
    K.CONVERSION_FUNCTION: "conversion",
    K.FUNCTION_TEMPLATE: "function_template",
}
RECORD_KINDS = {K.STRUCT_DECL, K.CLASS_DECL, K.UNION_DECL, K.CLASS_TEMPLATE, K.CLASS_TEMPLATE_PARTIAL_SPECIALIZATION}
TEMPLATE_PARAM_KINDS = {K.TEMPLATE_TYPE_PARAMETER, K.TEMPLATE_NON_TYPE_PARAMETER, K.TEMPLATE_TEMPLATE_PARAMETER}
DESCEND_KINDS = {K.NAMESPACE, K.LINKAGE_SPEC, K.UNEXPOSED_DECL}
SPECIALIZABLE = {K.STRUCT_DECL, K.CLASS_DECL, K.UNION_DECL, K.CLASS_TEMPLATE_PARTIAL_SPECIALIZATION}
FAMILY_RULES: list[tuple[str, tuple[str, ...]]] = [
    ("0 entry point", ("rack.hpp",)),
    (
        "1 common/value",
        (
            "arch.hpp",
            "common.hpp",
            "math.hpp",
            "color.hpp",
            "string.hpp",
            "random.hpp",
            "logger.hpp",
            "Quantity.hpp",
            "mutex.hpp",
            "weakptr.hpp",
        ),
    ),
    ("2 engine", ("engine/",)),
    ("3 plugin/model/assets/helpers", ("plugin.hpp", "plugin/", "asset.hpp", "helpers.hpp")),
    ("4 widgets/ui/app/componentlibrary", ("widget/", "ui/", "app/", "componentlibrary.hpp")),
    ("5 window/rendering/context", ("window/", "context.hpp")),
    ("6 dsp/simd", ("dsp/", "simd/")),
    ("7 audio/midi", ("audio.hpp", "midi.hpp")),
    ("8 services", ("settings.hpp", "system.hpp", "network.hpp")),
    ("9 history", ("history.hpp",)),
]

app = cyclopts.App(help=__doc__)


class ExtractionError(RuntimeError):
    """Raised when the inventory cannot be trusted; nothing is written."""


@dataclass(frozen=True)
class Variant:
    name: str
    platform: str
    flags: tuple[str, ...]
    note: str
    target: str | None = None


LINUX_FLAGS = ("-x", "c++", "-std=c++11", "-fPIC", "-funsafe-math-optimizations")
VARIANTS = {
    "lin-x64": Variant("lin-x64", "lin-x64", (*LINUX_FLAGS, "-march=nehalem"), "SDK flags for Linux x64"),
    "lin-x64-simde": Variant(
        "lin-x64-simde",
        "lin-x64",
        LINUX_FLAGS,
        "Linux x64 without -march=nehalem, so __SSE4_2__ is undefined and simd/common.hpp takes the SIMDe path that "
        "arm64 uses. Approximates the arm64 declaration surface; it is not an arm64 extraction.",
    ),
    "mac-arm64": Variant(
        "mac-arm64", "mac-arm64", ("-x", "c++", "-std=c++11", "-stdlib=libc++"), "Cross attempt", "arm64-apple-macos11"
    ),
    "mac-x64": Variant(
        "mac-x64",
        "mac-x64",
        ("-x", "c++", "-std=c++11", "-stdlib=libc++", "-march=nehalem"),
        "Cross attempt",
        "x86_64-apple-macos10.9",
    ),
    "win-x64": Variant(
        "win-x64",
        "win-x64",
        ("-x", "c++", "-std=c++11", "-march=nehalem", "-D_USE_MATH_DEFINES"),
        "Cross attempt",
        "x86_64-w64-mingw32",
    ),
}
NATIVE = ("lin-x64", "lin-x64-simde")


# --- libclang helpers -------------------------------------------------------


def find_libclang(clangxx: str) -> Path:
    """libclang.so from the same LLVM as `clangxx`.

    `-print-file-name` finds it where the library sits on the compiler's search
    path (Arch: /usr/lib). Debian and Ubuntu keep it in the LLVM tree instead
    (/usr/lib/llvm-N/lib), which is two levels above the resource directory.
    """
    found = subprocess.run([clangxx, "-print-file-name=libclang.so"], capture_output=True, text=True, check=True)
    direct = Path(found.stdout.strip())
    if direct.is_absolute() and direct.is_file():
        return direct
    resource = subprocess.run([clangxx, "-print-resource-dir"], capture_output=True, text=True, check=True)
    libdir = Path(resource.stdout.strip()).parents[1]
    for pattern in ("libclang.so", "libclang.so.*", "libclang-[0-9]*.so*"):
        if candidates := sorted(libdir.glob(pattern)):
            return candidates[0]
    return direct


def configure_libclang(libclang: Path | None, clangxx: str) -> tuple[str, str]:
    """Load libclang from the same LLVM as `clangxx`; return (libclang path, resource dir)."""
    resource = subprocess.run(
        [clangxx, "-print-resource-dir"], capture_output=True, text=True, check=True
    ).stdout.strip()
    if libclang is None:
        libclang = find_libclang(clangxx)
    if not libclang.is_file():
        raise ExtractionError(f"libclang not found at {libclang}; pass --libclang")
    if not ci.Config.loaded:
        ci.Config.set_library_file(str(libclang))
    lib = ci.conf.lib
    lib.clang_getCursorPlatformAvailability.argtypes = [
        ci.Cursor,
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ci._CXString),
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ci._CXString),
        ctypes.c_void_p,
        ctypes.c_int,
    ]
    lib.clang_getCursorPlatformAvailability.restype = ctypes.c_int
    lib.clang_Cursor_isMacroFunctionLike.argtypes = [ci.Cursor]
    lib.clang_Cursor_isMacroFunctionLike.restype = ctypes.c_uint
    lib.clang_getSkippedRanges.argtypes = [ci.TranslationUnit, ctypes.c_void_p]
    lib.clang_getSkippedRanges.restype = ctypes.POINTER(SourceRangeList)
    lib.clang_disposeSourceRangeList.argtypes = [ctypes.POINTER(SourceRangeList)]
    lib.clang_getFile.argtypes = [ci.TranslationUnit, ctypes.c_char_p]
    lib.clang_getFile.restype = ctypes.c_void_p
    lib.clang_getClangVersion.argtypes = []
    lib.clang_getClangVersion.restype = ci._CXString
    return str(libclang), resource


class SourceRangeList(ctypes.Structure):
    _fields_ = [("count", ctypes.c_uint), ("ranges", ctypes.POINTER(ci.SourceRange))]


def clang_version() -> str:
    return str(ci._CXString.from_result(ci.conf.lib.clang_getClangVersion()))


def deprecation(cursor: ci.Cursor) -> tuple[bool, str]:
    always = ctypes.c_int()
    message = ci._CXString()
    unavailable = ctypes.c_int()
    unavailable_message = ci._CXString()
    ci.conf.lib.clang_getCursorPlatformAvailability(
        cursor,
        ctypes.byref(always),
        ctypes.byref(message),
        ctypes.byref(unavailable),
        ctypes.byref(unavailable_message),
        None,
        0,
    )
    return bool(always.value), str(ci._CXString.from_result(message))


def skipped_lines(tu: ci.TranslationUnit, path: Path) -> list[tuple[int, int]]:
    handle = ci.conf.lib.clang_getFile(tu, str(path).encode())
    if not handle:
        return []
    ranges = ci.conf.lib.clang_getSkippedRanges(tu, handle)
    try:
        return [
            (ranges.contents.ranges[i].start.line, ranges.contents.ranges[i].end.line)
            for i in range(ranges.contents.count)
        ]
    finally:
        ci.conf.lib.clang_disposeSourceRangeList(ranges)


# --- preprocessor conditions ------------------------------------------------

DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")


def condition_map(text: str) -> list[tuple[str, ...]]:
    """Active #if condition stack for every line (index 0 is line 1)."""
    stack: list[str] = []
    result: list[tuple[str, ...]] = []
    for line in text.splitlines():
        m = DIRECTIVE.match(line)
        result.append(tuple(stack))
        if not m:
            continue
        kind, rest = m.group(1), " ".join(m.group(2).split("//")[0].split())
        if kind == "if":
            stack.append(rest)
        elif kind == "ifdef":
            stack.append(f"defined {rest}")
        elif kind == "ifndef":
            stack.append(f"!defined {rest}")
        elif kind in ("elif", "else") and stack:
            prior = stack.pop()
            stack.append(f"!({prior})" + (f" && {rest}" if kind == "elif" else ""))
        elif kind == "endif" and stack:
            stack.pop()
    return result


# --- extraction --------------------------------------------------------------


@dataclass
class Extraction:
    sdk: Path
    variant: Variant
    tu: ci.TranslationUnit
    conditions: dict[str, list[tuple[str, ...]]] = field(default_factory=dict)
    entries: dict[str, dict[str, Any]] = field(default_factory=dict)
    forward: dict[str, dict[str, Any]] = field(default_factory=dict)
    defined: set[str] = field(default_factory=set)
    blocks: list[dict[str, Any]] = field(default_factory=list)
    closure: list[str] = field(default_factory=list)
    collisions: list[str] = field(default_factory=list)
    deprecated_scopes: set[str] = field(default_factory=set)

    def header(self, cursor: ci.Cursor) -> str | None:
        f = cursor.location.file
        return self.header_of(f.name) if f else None

    def header_of(self, name: str) -> str | None:
        path = Path(name).resolve()
        include = (self.sdk / "include").resolve()
        return f"include/{path.relative_to(include).as_posix()}" if path.is_relative_to(include) else None


def parse(sdk: Path, variant: Variant, resource: str) -> ci.TranslationUnit:
    args = ["-resource-dir", resource, *variant.flags, f"-I{sdk / 'include'}", f"-I{sdk / 'dep' / 'include'}"]
    if variant.target:
        args += [f"--target={variant.target}"]
    index = ci.Index.create()
    options = ci.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD | ci.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES
    tu = index.parse(
        "rackpy-inventory.cpp",
        args=args,
        unsaved_files=[("rackpy-inventory.cpp", "#include <rack.hpp>\n")],
        options=options,
    )
    errors = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
    if errors:
        raise ExtractionError(
            f"{variant.name}: {len(errors)} error(s); first: {errors[0].spelling} at {errors[0].location}"
        )
    return tu


def qualified(cursor: ci.Cursor) -> str:
    parts: list[str] = []
    c: ci.Cursor | None = cursor
    while c is not None and c.kind != K.TRANSLATION_UNIT:
        if c.kind in DESCEND_KINDS - {K.NAMESPACE}:
            c = c.semantic_parent
            continue
        if c.kind in RECORD_KINDS | {K.ENUM_DECL} and c.is_anonymous() and c is not cursor:
            c = c.semantic_parent
            continue
        # Explicit and partial specializations are named with their arguments; templates are not.
        name = c.displayname if c.kind in SPECIALIZABLE and "<" in c.displayname else c.spelling
        if c.kind == K.ENUM_DECL and c.is_anonymous():
            first = next((e.spelling for e in c.get_children() if e.kind == K.ENUM_CONSTANT_DECL), "?")
            name = f"{{{first}}}"
        parts.append(name)
        c = c.semantic_parent
    return "::".join(reversed(parts))


def type_name(t: ci.Type, cursor: ci.Cursor) -> str:
    try:
        return " ".join(t.get_fully_qualified_name(ci.PrintingPolicy.create(cursor)).split())
    except Exception:
        return " ".join(t.spelling.split())


def template_params(cursor: ci.Cursor) -> list[dict[str, Any]]:
    params = []
    for child in cursor.get_children():
        if child.kind in TEMPLATE_PARAM_KINDS:
            tokens = [t.spelling for t in child.get_tokens()]
            default = " ".join(tokens[tokens.index("=") + 1 :]) if "=" in tokens else None
            kind = {K.TEMPLATE_TYPE_PARAMETER: "type", K.TEMPLATE_NON_TYPE_PARAMETER: "value"}.get(
                child.kind, "template"
            )
            param: dict[str, Any] = {"name": child.spelling, "kind": kind, "default": default}
            if child.kind == K.TEMPLATE_NON_TYPE_PARAMETER:
                param["type"] = type_name(child.type, child)
            if "..." in tokens:
                param["pack"] = True
            params.append(param)
    return params


def positional(text: str, tparams: list[dict[str, Any]], prefix: str = "") -> str:
    """Replace template parameter names with positions, so renaming them upstream keeps IDs stable."""
    for i, p in enumerate(tparams):
        if p["name"]:
            text = re.sub(rf"(?<![\w$]){re.escape(p['name'])}(?!\w)", f"${prefix}{i}", text)
    return text


def enclosing_template_params(cursor: ci.Cursor) -> list[dict[str, Any]]:
    params: list[dict[str, Any]] = []
    c = cursor.semantic_parent
    while c is not None and c.kind != K.TRANSLATION_UNIT:
        if c.kind in (K.CLASS_TEMPLATE, K.CLASS_TEMPLATE_PARTIAL_SPECIALIZATION):
            params = template_params(c) + params
        c = c.semantic_parent
    return params


def normalize(text: str, cursor: ci.Cursor, own: list[dict[str, Any]]) -> str:
    return positional(positional(text, own), enclosing_template_params(cursor), "c")


def default_tokens(parm: ci.Cursor) -> str | None:
    tokens = [t.spelling for t in parm.get_tokens()]
    return " ".join(tokens[tokens.index("=") + 1 :]) if "=" in tokens else None


def declaration_tokens(cursor: ci.Cursor) -> list[str]:
    out = []
    for t in cursor.get_tokens():
        if t.spelling in ("{", ";"):
            break
        out.append(t.spelling)
    return out


def referenced(t: ci.Type, ex: Extraction, seen: set[str]) -> None:
    """Collect origins of every named type inside `t`."""
    kind = t.kind
    if kind in (ci.TypeKind.POINTER, ci.TypeKind.LVALUEREFERENCE, ci.TypeKind.RVALUEREFERENCE):
        referenced(t.get_pointee(), ex, seen)
        return
    if kind in (ci.TypeKind.CONSTANTARRAY, ci.TypeKind.INCOMPLETEARRAY, ci.TypeKind.VARIABLEARRAY):
        referenced(t.element_type, ex, seen)
        return
    if kind in (ci.TypeKind.FUNCTIONPROTO, ci.TypeKind.FUNCTIONNOPROTO):
        referenced(t.get_result(), ex, seen)
        for a in t.argument_types():
            referenced(a, ex, seen)
        return
    for i in range(max(0, t.get_num_template_arguments())):
        referenced(t.get_template_argument_type(i), ex, seen)
    decl = t.get_declaration().canonical  # first declaration, so redeclarations report one header
    if decl.kind == K.NO_DECL_FOUND or not decl.location.file:
        return
    path = Path(decl.location.file.name).resolve()
    dep = (ex.sdk / "dep" / "include").resolve()
    if path.is_relative_to(dep):
        seen.add(f"{path.relative_to(dep).as_posix()}:{decl.spelling}")
    elif not path.is_relative_to((ex.sdk / "include").resolve()):
        name = decl.spelling
        parent = decl.semantic_parent
        while parent is not None and parent.kind == K.NAMESPACE:
            if not parent.spelling.startswith("__"):
                name = f"{parent.spelling}::{name}"
            parent = parent.semantic_parent
        seen.add(f"std:{name}" if name.startswith("std::") else f"system:{name}")


def comment_flags(cursor: ci.Cursor) -> dict[str, bool]:
    raw = cursor.raw_comment or ""
    flags = {}
    if re.search(r"\bDEPRECATED\b|\bDeprecated\b", raw):
        flags["comment_deprecated"] = True
    if "Unstable API" in raw:
        flags["unstable"] = True
    return flags


def base_entry(ex: Extraction, cursor: ci.Cursor, kind: str, ident: str, header: str) -> dict[str, Any]:
    line = cursor.location.line
    entry: dict[str, Any] = {
        "id": ident,
        "kind": kind,
        "name": qualified(cursor),
        "header": header,
        "line": line,
        "access": cursor.access_specifier.name.lower() if cursor.access_specifier.name != "INVALID" else "namespace",
    }
    conds = ex.conditions[header][line - 1] if line - 1 < len(ex.conditions[header]) else ()
    if conds:
        entry["conditions"] = list(conds)
    deprecated, message = deprecation(cursor)
    if deprecated and message == PRIVATE_MESSAGE:
        entry["private"] = True
    elif deprecated:
        entry["deprecated"] = message or True
    entry.update(comment_flags(cursor))
    # Declared inside its own scope, as opposed to an out-of-line redefinition of the same entity.
    lexical, semantic = cursor.lexical_parent, cursor.semantic_parent
    entry["_in_scope"] = lexical is None or semantic is None or lexical == semantic
    return entry


def add(
    ex: Extraction, entry: dict[str, Any], types: list[ci.Type] | None = None, cursor: ci.Cursor | None = None
) -> None:
    if types and cursor is not None:
        seen: set[str] = set()
        for t in types:
            referenced(t, ex, seen)
        if seen:
            entry["types"] = sorted(seen)
    existing = ex.entries.get(entry["id"])
    if existing is None:
        ex.entries[entry["id"]] = entry
    else:
        same_place = (existing["header"], existing["line"]) == (entry["header"], entry["line"])
        if existing.get("_in_scope") and entry.get("_in_scope") and not same_place:
            ex.collisions.append(
                f"{entry['id']} at {existing['header']}:{existing['line']} and {entry['header']}:{entry['line']}"
            )
        # Redeclaration (for example an out-of-line template member); keep the first, merge flags.
        for key in ("private", "deprecated", "comment_deprecated", "unstable"):
            if key in entry and key not in existing:
                existing[key] = entry[key]


def visit(ex: Extraction, cursor: ci.Cursor, anon_group: str | None = None) -> None:
    for child in cursor.get_children():
        header = ex.header(child)
        kind = child.kind
        if kind in DESCEND_KINDS:
            if header is not None:
                if kind == K.NAMESPACE and comment_flags(child).get("comment_deprecated"):
                    ex.deprecated_scopes.add(qualified(child))
                visit(ex, child, anon_group)
            continue
        if header is None:
            continue
        if kind in RECORD_KINDS:
            record(ex, child, header, anon_group)
        elif kind in FUNCTION_KINDS:
            function(ex, child, header)
        elif kind == K.FIELD_DECL:
            fld(ex, child, header, anon_group)
        elif kind == K.VAR_DECL:
            variable(ex, child, header)
        elif kind == K.ENUM_DECL:
            enum(ex, child, header)
        elif kind in (K.TYPEDEF_DECL, K.TYPE_ALIAS_DECL):
            alias(ex, child, header)
        elif kind == K.TYPE_ALIAS_TEMPLATE_DECL:
            tparams = template_params(child)
            ident = f"alias_template:{qualified(child)}<{', '.join(p['kind'] for p in tparams)}>"
            entry = base_entry(ex, child, "alias_template", ident, header)
            entry["template_params"] = tparams
            add(ex, entry)
        elif kind in (K.USING_DECLARATION, K.USING_DIRECTIVE, K.NAMESPACE_ALIAS):
            target = re.sub(r"\s*::\s*", "::", " ".join(t.spelling for t in child.get_tokens())).rstrip(";").strip()
            scope = qualified(child.semantic_parent) if child.semantic_parent else ""
            entry = base_entry(ex, child, "import", f"import:{scope}:{target}", header)
            entry["name"] = scope
            entry["target"] = target
            add(ex, entry)
        elif kind == K.FRIEND_DECL:
            visit(ex, child, anon_group)
        elif kind == K.MACRO_DEFINITION:
            macro(ex, child, header)


def record(ex: Extraction, cursor: ci.Cursor, header: str, anon_group: str | None) -> None:
    if cursor.is_anonymous() and cursor.kind != K.CLASS_TEMPLATE:
        # Members of an anonymous union or struct belong to the enclosing record.
        visit(ex, cursor, f"{qualified(cursor.semantic_parent)}@{cursor.location.line}")
        return
    name = qualified(cursor)
    kind = {
        K.CLASS_TEMPLATE: "class_template",
        K.CLASS_TEMPLATE_PARTIAL_SPECIALIZATION: "partial_specialization",
    }.get(cursor.kind, "specialization" if "<" in cursor.displayname else "record")
    tparams = template_params(cursor) if kind in ("class_template", "partial_specialization") else []
    ident = f"{kind}:{name}" + (f"<{', '.join(p['kind'] for p in tparams)}>" if kind == "class_template" else "")
    if not cursor.is_definition():
        ex.forward.setdefault(
            ident, base_entry(ex, cursor, "opaque_record", ident.replace(f"{kind}:", "opaque_record:", 1), header)
        )
        return
    ex.defined.add(ident)
    entry = base_entry(ex, cursor, kind, ident, header)
    entry["tag"] = {K.UNION_DECL: "union", K.CLASS_DECL: "class"}.get(cursor.kind, "struct")
    if tparams:
        entry["template_params"] = tparams
    bases = []
    types = []
    for child in cursor.get_children():
        if child.kind == K.CXX_BASE_SPECIFIER:
            bases.append(
                {
                    "type": type_name(child.type, child),
                    "access": child.access_specifier.name.lower(),
                    "virtual": child.is_virtual_base(),
                }
            )
            types.append(child.type)
    if bases:
        entry["bases"] = bases
    if cursor.is_abstract_record():
        entry["abstract"] = True
    if anon_group:
        entry["anonymous_group"] = anon_group
    add(ex, entry, types, cursor)
    visit(ex, cursor)


def function(ex: Extraction, cursor: ci.Cursor, header: str) -> None:
    kind = FUNCTION_KINDS[cursor.kind]
    parent = cursor.semantic_parent
    if kind == "function_template" and parent is not None and parent.kind in RECORD_KINDS:
        kind = "method_template"
    if kind == "function" and parent is not None and parent.kind in RECORD_KINDS:
        kind = "method"
    tparams = template_params(cursor) if kind.endswith("_template") else []
    parms = [c for c in cursor.get_children() if c.kind == K.PARM_DECL]
    params = [
        {"name": p.spelling, "type": normalize(type_name(p.type, p), cursor, tparams), "default": default_tokens(p)}
        for p in parms
    ]
    for p in params:
        if p["default"] is None:
            del p["default"]
    variadic = (
        cursor.kind != K.FUNCTION_TEMPLATE
        and cursor.type.kind == ci.TypeKind.FUNCTIONPROTO
        and cursor.type.is_function_variadic()
    )
    sig = ", ".join(p["type"] for p in params) + (", ..." if variadic and params else "..." if variadic else "")
    quals = ""
    if cursor.kind in (K.CXX_METHOD, K.FUNCTION_TEMPLATE, K.CONVERSION_FUNCTION) and cursor.is_const_method():
        quals += " const"
    tsig = f"<{', '.join(p['kind'] + ('...' if p.get('pack') else '') for p in tparams)}>" if tparams else ""
    specialization = []
    if cursor.kind != K.FUNCTION_TEMPLATE and cursor.get_num_template_arguments() > 0:
        # Explicit specialization of a function template, e.g. random::get<uint8_t>().
        for i in range(cursor.get_num_template_arguments()):
            if cursor.get_template_argument_kind(i) == ci.TemplateArgumentKind.TYPE:
                arg = cursor.get_template_argument_type(i)
                # Template arguments come back canonical (uint64_t -> unsigned long on Linux only). Prefer the
                # written spelling of a matching return or parameter type so IDs stay platform-independent.
                written = [cursor.result_type] + [p.type for p in parms]
                match = next((t for t in written if t.get_canonical().spelling == arg.get_canonical().spelling), None)
                specialization.append(type_name(match if match is not None else arg, cursor))
            else:
                specialization.append(str(cursor.get_template_argument_value(i)))
        tsig = f"<{', '.join(specialization)}>"
    ident = f"{kind}:{qualified(cursor)}{tsig}({sig}){quals}"
    entry = base_entry(ex, cursor, kind, ident, header)
    entry["params"] = params
    if specialization:
        entry["specialization_args"] = specialization
    if kind not in ("constructor", "destructor"):
        entry["returns"] = normalize(type_name(cursor.result_type, cursor), cursor, tparams)
    if tparams:
        entry["template_params"] = tparams
    tokens = declaration_tokens(cursor)
    flags = {
        "static": cursor.is_static_method() if cursor.kind != K.FUNCTION_DECL else False,
        "virtual": cursor.is_virtual_method(),
        "pure": cursor.is_pure_virtual_method(),
        "deleted": cursor.is_deleted_method(),
        "defaulted": cursor.is_default_method(),
        "explicit": cursor.kind == K.CONSTRUCTOR and cursor.is_explicit_method(),
        "override": "override" in tokens,
        "final": "final" in tokens,
        "inline": "inline" in tokens,
        "variadic": variadic,
        "extern_c": cursor.linkage == ci.LinkageKind.EXTERNAL
        and parent is not None
        and parent.kind == K.TRANSLATION_UNIT,
        "friend": cursor.lexical_parent is not None and cursor.lexical_parent.kind == K.FRIEND_DECL,
    }
    entry.update({k: True for k, v in flags.items() if v})
    if cursor.kind in (K.FUNCTION_DECL, K.CXX_METHOD, K.CONSTRUCTOR, K.DESTRUCTOR, K.CONVERSION_FUNCTION):
        spec = cursor.exception_specification_kind
        if spec in (ci.ExceptionSpecificationKind.BASIC_NOEXCEPT, ci.ExceptionSpecificationKind.DYNAMIC_NONE):
            entry["noexcept"] = True
    add(ex, entry, [p.type for p in parms] + [cursor.result_type], cursor)


def fld(ex: Extraction, cursor: ci.Cursor, header: str, anon_group: str | None) -> None:
    ident = f"field:{qualified(cursor)}"
    entry = base_entry(ex, cursor, "field", ident, header)
    entry["type"] = normalize(type_name(cursor.type, cursor), cursor, [])
    if cursor.is_bitfield():
        entry["bitfield"] = cursor.get_bitfield_width()
    if anon_group:
        entry["anonymous_group"] = anon_group
    decl = cursor.type.get_pointee().get_declaration()
    if decl.spelling == "Internal" and not decl.is_definition():
        entry["pimpl"] = True
    add(ex, entry, [cursor.type], cursor)


def variable(ex: Extraction, cursor: ci.Cursor, header: str) -> None:
    parent = cursor.semantic_parent
    kind = "static_field" if parent is not None and parent.kind in RECORD_KINDS else "variable"
    entry = base_entry(ex, cursor, kind, f"{kind}:{qualified(cursor)}", header)
    entry["type"] = type_name(cursor.type, cursor)
    tokens = declaration_tokens(cursor)
    if "=" in tokens:
        entry["value"] = " ".join(tokens[tokens.index("=") + 1 :])
    if "extern" in tokens:
        entry["extern"] = True
    add(ex, entry, [cursor.type], cursor)


def enum(ex: Extraction, cursor: ci.Cursor, header: str) -> None:
    name = qualified(cursor)
    entry = base_entry(ex, cursor, "enum", f"enum:{name}", header)
    entry["scoped"] = cursor.is_scoped_enum()
    entry["underlying"] = type_name(cursor.enum_type, cursor)
    add(ex, entry)
    for child in cursor.get_children():
        if child.kind == K.ENUM_CONSTANT_DECL:
            # The ID nests under the enum; `name` is where C++ makes the enumerator visible.
            e = base_entry(ex, child, "enumerator", f"enumerator:{name}::{child.spelling}", header)
            e["value"] = child.enum_value
            if not cursor.is_scoped_enum() and cursor.semantic_parent is not None:
                # Unscoped enumerators are also visible in the enclosing scope.
                outer = qualified(cursor.semantic_parent)
                e["visible_as"] = f"{outer}::{child.spelling}" if outer else child.spelling
            add(ex, e)


def alias(ex: Extraction, cursor: ci.Cursor, header: str) -> None:
    entry = base_entry(ex, cursor, "alias", f"alias:{qualified(cursor)}", header)
    entry["target"] = type_name(cursor.underlying_typedef_type, cursor)
    add(ex, entry, [cursor.underlying_typedef_type], cursor)


def macro(ex: Extraction, cursor: ci.Cursor, header: str) -> None:
    tokens = [t.spelling for t in cursor.get_tokens()]
    function_like = bool(ci.conf.lib.clang_Cursor_isMacroFunctionLike(cursor))
    params: list[str] = []
    body = tokens[1:]
    if function_like and len(tokens) > 1 and tokens[1] == "(":
        close = tokens.index(")")
        params = [t for t in tokens[2:close] if t != ","]
        body = tokens[close + 1 :]
    line = cursor.location.line
    entry: dict[str, Any] = {
        "id": f"macro:{cursor.spelling}",
        "kind": "function_macro" if function_like else "object_macro",
        "name": cursor.spelling,
        "header": header,
        "line": line,
        "body": " ".join(body),
    }
    if function_like:
        entry["params"] = params
    conds = ex.conditions[header][line - 1]
    if conds:
        entry["conditions"] = list(conds)
    add(ex, entry)


DECL_NAME = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
MACRO_NAME = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)")


NOT_DECLARATIONS = {
    "static_assert",
    "sizeof",
    "defined",
    "__attribute__",
    "aligned",
    "optimize",
    "__declspec",
    "alignas",
}


def strip_comments(lines: list[str]) -> list[str]:
    """Code part of each line, tracking /* */ across lines."""
    out, in_block = [], False
    for line in lines:
        code, i = [], 0
        while i < len(line):
            if in_block:
                end = line.find("*/", i)
                if end < 0:
                    break
                in_block, i = False, end + 2
            elif line.startswith("//", i):
                break
            elif line.startswith("/*", i):
                in_block, i = True, i + 2
            else:
                code.append(line[i])
                i += 1
        out.append("".join(code))
    return out


def conditional_blocks(ex: Extraction, headers: list[str]) -> None:
    """Record code that this variant's preprocessor skipped, so other platforms' declarations are not lost."""
    for header in headers:
        path = ex.sdk / header
        lines = path.read_text(encoding="utf-8").splitlines()
        for start, end in skipped_lines(ex.tu, path):
            body = lines[start - 1 : end]
            names = set()
            for text in strip_comments(body[1:-1]):
                m = MACRO_NAME.match(text)
                if m:
                    names.add(f"macro {m.group(1)}")
                elif not text.strip().startswith("#"):
                    names.update(f"function {n}" for n in DECL_NAME.findall(text) if n not in NOT_DECLARATIONS)
            ex.blocks.append(
                {
                    "header": header,
                    "lines": [start, end],
                    "condition": " ".join(body[0].split()) if body else "",
                    "sha256": hashlib.sha256("\n".join(body).encode()).hexdigest(),
                    "declarations": sorted(names),
                }
            )


def extract_variant(sdk: Path, variant: Variant, resource: str) -> Extraction:
    tu = parse(sdk, variant, resource)
    ex = Extraction(sdk=sdk, variant=variant, tu=tu)
    closure = sorted(
        {h for h in (ex.header_of(inc.include.name) for inc in tu.get_includes()) if h} | {"include/rack.hpp"}
    )
    ex.closure = closure
    for header in closure:
        ex.conditions[header] = condition_map((sdk / header).read_text(encoding="utf-8"))
    visit(ex, tu.cursor)
    for ident, entry in ex.forward.items():
        if ident not in ex.defined:
            add(ex, entry)
    conditional_blocks(ex, closure)
    for entry in ex.entries.values():
        entry.pop("_in_scope", None)
        # A namespace documented as deprecated (rack::event) deprecates everything in it.
        scope = next((s for s in sorted(ex.deprecated_scopes) if entry["name"].startswith(f"{s}::")), None)
        if scope and not entry.get("comment_deprecated"):
            entry["comment_deprecated"] = True
            entry["deprecated_scope"] = scope
    for entry in ex.forward.values():
        entry.pop("_in_scope", None)
    if ex.collisions:
        raise ExtractionError(
            f"{variant.name}: {len(ex.collisions)} distinct declarations share an ID: {ex.collisions[:5]}"
        )
    if not ex.entries:
        raise ExtractionError(f"{variant.name}: no declarations found; refusing to write an empty inventory")
    return ex


# --- classification and output ----------------------------------------------


def classify(entry: dict[str, Any]) -> tuple[str, str]:
    if entry["kind"] == "import":
        return "import", "using-declaration or directive; not a declaration"
    if entry.get("private"):
        return "excluded_internal", "PRIVATE attribute"
    if entry.get("pimpl") or (entry["name"].endswith("::Internal") and entry["kind"] == "opaque_record"):
        return "excluded_internal", "pimpl Internal type is incomplete in the rack.hpp closure"
    if entry.get("access") == "private":
        return "inaccessible", "C++ private access"
    if entry["kind"] == "opaque_record":
        return "public_opaque", "declared but never defined in the rack.hpp closure"
    return "public", "reachable from rack.hpp"


def family(header: str) -> str:
    rel = header.removeprefix("include/")
    for name, prefixes in FAMILY_RULES:
        if any(rel == p or (p.endswith("/") and rel.startswith(p)) for p in prefixes):
            return name
    return "unassigned"


def merge(extractions: dict[str, Extraction]) -> list[dict[str, Any]]:
    merged: dict[str, dict[str, Any]] = {}
    for name, ex in extractions.items():
        for ident, entry in ex.entries.items():
            merged.setdefault(ident, {**entry, "availability": set()})["availability"].add(name)
    for entry in merged.values():
        present = entry.pop("availability")
        absent = sorted(name for name in extractions if name not in present)
        if absent:
            entry["absent_in"] = absent
        entry["classification"], evidence = classify(entry)
        if entry["classification"] != "public":
            entry["evidence"] = evidence
    return [merged[k] for k in sorted(merged)]


def sdk_facts(sdk: Path) -> dict[str, Any]:
    versions = json.loads(VERSIONS.read_text(encoding="utf-8"))
    lin = next(a for a in versions["sdk"]["archives"] if a["platform"] == "lin-x64")
    return {
        "rack_commit": versions["rack"]["commit"],
        "archive": lin["url"],
        "archive_sha256": lin["sha256"],
        "parsed_root": "Rack-SDK (lin-x64 archive)",
    }


def build(sdk: Path, libclang: Path | None, clangxx: str, variants: tuple[str, ...]) -> dict[str, Any]:
    lib_path, resource = configure_libclang(libclang, clangxx)
    extractions: dict[str, Extraction] = {}
    runs: list[dict[str, Any]] = []
    for name in variants:
        variant = VARIANTS[name]
        record_run: dict[str, Any] = {
            "variant": name,
            "platform": variant.platform,
            "note": variant.note,
            "flags": list(variant.flags) + ([f"--target={variant.target}"] if variant.target else []),
        }
        try:
            ex = extract_variant(sdk, variant, resource)
        except ExtractionError as e:
            if name in NATIVE:
                raise
            record_run["status"] = "not_extracted"
            record_run["reason"] = str(e).split(" at ")[0]
            runs.append(record_run)
            continue
        record_run["status"] = "ok"
        record_run["headers"] = len(ex.closure)
        record_run["declarations"] = len(ex.entries)
        extractions[name] = ex
        runs.append(record_run)
    entries = merge(extractions)
    blocks: dict[tuple[str, int], dict[str, Any]] = {}
    for name, ex in extractions.items():
        for b in ex.blocks:
            key = (b["header"], b["lines"][0])
            blocks.setdefault(key, {**b, "skipped_in": []})["skipped_in"].append(name)
    boundary = Counter(
        t
        for e in entries
        if e["classification"] == "public"
        for t in e.get("types", ())
        if not t.startswith(("std:", "system:"))
    )
    return {
        "schema_version": SCHEMA_VERSION,
        "generator": "bin/inventory_sdk.py",
        "note": "Inventory of declarations reachable from rack.hpp. Not a coverage report.",
        "sdk": sdk_facts(sdk),
        "toolchain": {"libclang": clang_version(), "bindings": BINDINGS, "libclang_sha256": sha256(Path(lib_path))},
        "extractions": runs,
        "conditional_blocks": [blocks[k] for k in sorted(blocks)],
        "boundary_types": [{"type": t, "public_uses": n} for t, n in sorted(boundary.items())],
        "families": {h: family(h) for h in sorted({e["header"] for e in entries})},
        "summary": summarize(entries),
        "entries": entries,
    }


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(entries: list[dict[str, Any]]) -> dict[str, Any]:
    counted = [e for e in entries if e["classification"] != "import"]
    return {
        "entries": len(entries),
        "by_classification": dict(sorted(Counter(e["classification"] for e in entries).items())),
        "by_kind": dict(sorted(Counter(e["kind"] for e in counted).items())),
        "by_family": {
            fam: dict(sorted(Counter(e["classification"] for e in counted if family(e["header"]) == fam).items()))
            for fam in sorted({family(e["header"]) for e in counted})
        },
        "deprecated": sum(1 for e in counted if e.get("deprecated") or e.get("comment_deprecated")),
        "unstable": sum(1 for e in counted if e.get("unstable")),
        "platform_conditional": sum(1 for e in counted if e.get("conditions")),
        "variant_differences": sum(1 for e in counted if e.get("absent_in")),
    }


def dumps(doc: dict[str, Any]) -> str:
    """Pretty top level, one compact entry per line, so diffs stay reviewable."""
    head = {k: v for k, v in doc.items() if k != "entries"}
    text = json.dumps(head, indent=2, sort_keys=True, ensure_ascii=False)
    lines = ",\n".join("    " + json.dumps(e, sort_keys=True, ensure_ascii=False) for e in doc["entries"])
    return text[:-2] + ',\n  "entries": [\n' + lines + "\n  ]\n}\n"


def diff(old: dict[str, Any], new: dict[str, Any]) -> dict[str, list[str]]:
    a = {e["id"]: e for e in old["entries"]}
    b = {e["id"]: e for e in new["entries"]}
    changed = sorted(k for k in a.keys() & b.keys() if {**a[k], "line": 0} != {**b[k], "line": 0})
    blocks_a = {(x["header"], x["sha256"]) for x in old["conditional_blocks"]}
    blocks_b = {(x["header"], x["sha256"]) for x in new["conditional_blocks"]}
    return {
        "added": sorted(b.keys() - a.keys()),
        "removed": sorted(a.keys() - b.keys()),
        "changed": changed,
        "conditional_blocks_added": sorted(f"{h} {s[:12]}" for h, s in blocks_b - blocks_a),
        "conditional_blocks_removed": sorted(f"{h} {s[:12]}" for h, s in blocks_a - blocks_b),
    }


def summary_markdown(doc: dict[str, Any]) -> str:
    s = doc["summary"]
    out = [
        "# Rack SDK surface summary",
        "",
        "**Generated by `bin/inventory_sdk.py summary --write`; do not edit by hand.** This counts declarations",
        "that exist in the pinned SDK. It says nothing about what RackPy has bound.",
        "",
        f"Rack commit `{doc['sdk']['rack_commit']}`; {doc['toolchain']['libclang']}.",
        "",
        "## Extractions",
        "",
        "| Variant | Status | Declarations | Note |",
        "| --- | --- | --- | --- |",
    ]
    for r in doc["extractions"]:
        note = r.get("reason") or r["note"]
        out.append(f"| {r['variant']} | {r['status']} | {r.get('declarations', '')} | {note} |")
    classes = sorted(c for c in s["by_classification"] if c != "import")
    out += ["", "## By family", "", "| Family | " + " | ".join(classes) + " |", "| --- |" + " --- |" * len(classes)]
    for fam, counts in s["by_family"].items():
        out.append(f"| {fam} | " + " | ".join(str(counts.get(c, 0)) for c in classes) + " |")
    out += ["", "## By kind", "", "| Kind | Entries |", "| --- | --- |"]
    out += [f"| {k} | {n} |" for k, n in s["by_kind"].items()]
    out += [
        "",
        "## Other counts",
        "",
        f"- Imports (`using` declarations and directives; not declarations): {s['by_classification'].get('import', 0)}",
        f"- Deprecated (attribute or comment): {s['deprecated']}",
        f"- Documented as unstable: {s['unstable']}",
        f"- Inside a platform conditional: {s['platform_conditional']}",
        f"- Present in only some variants: {s['variant_differences']}",
        f"- Conditional blocks skipped by a variant: {len(doc['conditional_blocks'])}",
        f"- Dependency boundary types used by public declarations: {len(doc['boundary_types'])}",
        "",
    ]
    return "\n".join(out)


# --- commands -----------------------------------------------------------------

SdkOpt = Annotated[Path | None, cyclopts.Parameter(help="Extracted SDK root; default: `bin/rack-sdk path`.")]
LibOpt = Annotated[Path | None, cyclopts.Parameter(help="libclang.so from the same LLVM as --clangxx.")]


def resolve_sdk(sdk: Path | None) -> Path:
    if sdk is not None:
        return sdk
    out = subprocess.run([str(REPO / "bin" / "rack-sdk"), "path"], capture_output=True, text=True, check=True)
    return Path(out.stdout.strip())


@app.command
def extract(
    *,
    sdk: SdkOpt = None,
    libclang: LibOpt = None,
    clangxx: str = "clang++",
    output: Path = OUTPUT,
    variants: tuple[str, ...] = tuple(VARIANTS),
) -> None:
    """Extract the inventory and write it (default upstream/api-surface.json)."""
    doc = build(resolve_sdk(sdk), libclang, clangxx, variants)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(dumps(doc), encoding="utf-8")
    print(f"{doc['summary']['entries']} entries; {doc['summary']['by_classification']}")


@app.command
def check(*, sdk: SdkOpt = None, libclang: LibOpt = None, clangxx: str = "clang++", against: Path = OUTPUT) -> None:
    """Re-extract and fail if the result differs from the committed inventory."""
    recorded = json.loads(against.read_text(encoding="utf-8"))
    variants = tuple(r["variant"] for r in recorded["extractions"])
    doc = build(resolve_sdk(sdk), libclang, clangxx, variants)
    if dumps(doc) != against.read_text(encoding="utf-8"):
        delta = diff(recorded, doc)
        detail = {k: v[:20] for k, v in delta.items() if v}
        raise SystemExit(f"inventory differs from {against}:\n{json.dumps(detail, indent=2)}")
    print(f"ok: {doc['summary']['entries']} entries match {against}")


@app.command(name="diff")
def diff_cmd(old: Path, new: Path) -> None:
    """Report added, removed and changed entries between two inventories."""
    delta = diff(json.loads(old.read_text(encoding="utf-8")), json.loads(new.read_text(encoding="utf-8")))
    print(json.dumps(delta, indent=2))
    if any(delta.values()):
        raise SystemExit(1)


@app.command
def summary(*, inventory: Path = OUTPUT, write: bool = False) -> None:
    """Print the human family summary; with --write, update docs/reference/surface-summary.md."""
    text = summary_markdown(json.loads(inventory.read_text(encoding="utf-8")))
    if write:
        SUMMARY.parent.mkdir(parents=True, exist_ok=True)
        SUMMARY.write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    if shutil.which("clang++") is None:
        raise SystemExit("clang++ is required")
    app()
