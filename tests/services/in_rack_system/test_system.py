# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-30B: rack::system and rack::network inside Rack, with temporary directories and a loopback HTTP server."""

import inspect
import io
import json
import os
import tarfile
import threading
from collections.abc import Callable, Iterator
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

import pytest

from rackpy.raw import network as rn
from rackpy.raw import system as rsys
from rackpy.services import network as net
from rackpy.services import system as files

os.environ["no_proxy"] = os.environ["NO_PROXY"] = "*"  # curl reads these; the server is on loopback
BLOB = bytes(range(256)) * 400


# Rack 2.6.6's header examples, with the results Rack 2.6.6 actually gives where they differ (marked).
PATH_EXAMPLES = [
    ("get_directory", "/var/tmp/example.txt", "/var/tmp"),
    ("get_directory", "/", "/"),  # the header says ""
    ("get_directory", "/var/tmp/.", "/var/tmp"),
    ("get_filename", "/foo/bar.txt", "bar.txt"),
    ("get_filename", "/foo/.bar", ".bar"),
    ("get_filename", "/foo/bar/", ""),  # the header says "."
    ("get_filename", "/foo/.", "."),
    ("get_filename", "/foo/..", ".."),
    ("get_filename", ".", "."),
    ("get_filename", "..", ".."),
    ("get_filename", "/", ""),  # the header says "/"
    ("get_stem", "/foo/bar.txt", "bar"),
    ("get_stem", "/foo/.bar", ".bar"),  # the header says ""
    ("get_stem", "/foo/foo.tar.ztd", "foo.tar"),
    ("get_extension", "/foo/bar.txt", ".txt"),
    ("get_extension", "/foo/bar.", "."),
    ("get_extension", "/foo/bar", ""),
    ("get_extension", "/foo/bar.txt/bar.cc", ".cc"),
    ("get_extension", "/foo/bar.txt/bar.", "."),
    ("get_extension", "/foo/bar.txt/bar", ""),
    ("get_extension", "/foo/.", ""),
    ("get_extension", "/foo/..", "."),  # the header says ""
    ("get_extension", "/foo/.hidden", ""),  # the header says ".hidden"
]


def test_paths_as_rack_documents_them() -> None:
    # Rack 2.6.6's join("/a", "") gives "/a/" (its header says path1).
    assert (
        rsys.join("/a", "b") == "/a/b" and rsys.join("/a", "") == "/a/" and rsys.join("/a", "b", "c", "d") == "/a/b/c/d"
    )
    wrong = [(f, p, want, getattr(rsys, f)(p)) for f, p, want in PATH_EXAMPLES if getattr(rsys, f)(p) != want]
    assert wrong == [], repr(wrong)
    assert rsys.get_filename(Path("/x/y.z")) == "y.z"  # os.PathLike too


def test_files_and_directories(tmp_path: Path) -> None:
    f = tmp_path / "a.bin"
    rsys.write_file(f, b"rack\x00py")
    assert rsys.read_file(f) == b"rack\x00py" and rsys.get_file_size(f) == 7
    assert rsys.exists(f) and rsys.is_file(f) and not rsys.is_directory(f)
    with pytest.raises(RuntimeError):
        rsys.read_file(tmp_path / "missing")  # Rack's Exception
    assert rsys.copy(f, tmp_path / "b.bin") and rsys.rename(tmp_path / "b.bin", tmp_path / "c.bin")
    assert rsys.create_directory(tmp_path / "d") and not rsys.create_directory(tmp_path / "x" / "y")  # parent missing
    assert rsys.create_directories(tmp_path / "x" / "y" / "z")
    assert rsys.create_symbolic_link(f, tmp_path / "link")
    assert sorted(Path(p).name for p in rsys.get_entries(tmp_path)) == ["a.bin", "c.bin", "d", "link", "x"]
    assert len(rsys.get_entries(tmp_path, -1)) == 7
    assert rsys.remove(tmp_path / "c.bin") and rsys.remove_recursively(tmp_path / "x") == 3
    assert rsys.get_canonical(tmp_path / "d" / ".." / "a.bin") == str((tmp_path / "a.bin").resolve())
    assert rsys.get_absolute("rel") == str(Path.cwd() / "rel")
    here = rsys.get_working_directory()
    rsys.set_working_directory(tmp_path)
    try:
        assert rsys.get_working_directory() == str(tmp_path)
    finally:
        rsys.set_working_directory(here)
    assert Path(rsys.get_temp_directory()).is_dir() and files.temp_directory().is_dir()
    assert [p.name for p in files.entries(tmp_path / "d")] == []


def test_archives(tmp_path: Path) -> None:
    src = tmp_path / "src"
    (src / "sub").mkdir(parents=True)
    (src / "sub" / "data.bin").write_bytes(BLOB)
    (src / "gone.txt").write_bytes(b"")  # Rack's convention: a zero-byte file deletes the existing one
    data = rsys.archive_directory_to_bytes(src)
    assert files.archive(src) is not None
    out = tmp_path / "out"
    out.mkdir()
    (out / "gone.txt").write_text("old")
    rsys.unarchive_to_directory(data, out)
    assert (out / "sub" / "data.bin").read_bytes() == BLOB and not (out / "gone.txt").exists()
    archive = tmp_path / "a.tar.zst"
    rsys.archive_directory(archive, src, 3)
    again = tmp_path / "again"
    again.mkdir()
    files.unarchive(archive, again)
    assert (again / "sub" / "data.bin").read_bytes() == BLOB


def test_rack_follows_dotdot_entries_and_the_facade_refuses_them(tmp_path: Path) -> None:
    buf = io.BytesIO()
    mode: Any = "w:zst"  # Python 3.14's tarfile writes Zstandard (typeshed's overloads lack it)
    with tarfile.open(fileobj=buf, mode=mode) as tar:
        member = tarfile.TarInfo("../escaped.txt")
        member.size = 5
        tar.addfile(member, io.BytesIO(b"hello"))
    evil = buf.getvalue()
    target = tmp_path / "inner"
    target.mkdir()
    with pytest.raises(ValueError, match="outside"):
        files.unarchive(evil, target)
    assert not (tmp_path / "escaped.txt").exists()
    rsys.unarchive_to_directory(evil, target)  # Rack's own unarchiver writes outside the directory
    assert (tmp_path / "escaped.txt").read_bytes() == b"hello"


def test_time_processes_and_flags() -> None:
    t0 = rsys.get_time()
    rsys.sleep(0.01)
    assert rsys.get_time() - t0 >= 0.009 and rsys.get_unix_time() > 1.6e9 and rsys.get_thread_time() >= 0
    with pytest.raises(ValueError):
        rsys.sleep(-1)
    assert rsys.get_logical_core_count() >= 1 and "Linux" in rsys.get_operating_system_info()
    assert isinstance(rsys.get_stack_trace(), str)
    names: list[str] = []

    def name_thread() -> None:
        rsys.set_thread_name("rackpy-worker")
        names.append("ok")

    worker = threading.Thread(target=name_thread)
    worker.start()
    worker.join()
    assert names == ["ok"]
    flags = rsys.get_fpu_flags()
    try:
        rsys.reset_fpu_flags()
        assert rsys.get_fpu_flags() & 0x8040 == 0x8040  # Rack's flush-to-zero and denormals-are-zero
    finally:
        rsys.set_fpu_flags(flags)
    assert rsys.get_fpu_flags() == flags
    rsys.open_browser("")  # blank: Rack does nothing
    rsys.open_directory("")
    with pytest.raises(ValueError, match="shell"):
        rsys.open_browser('https://example.com/"; touch /tmp/x; "')
    with pytest.raises(ValueError, match="Windows only"):
        rsys.run_process_detached("/bin/true")


class Handler(BaseHTTPRequestHandler):
    def log_message(self, format: str, *args: object) -> None:
        pass

    def reply(self, status: int, body: bytes, kind: str = "application/json") -> None:
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def answer(self) -> None:
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length).decode() if length else ""
        if self.path.startswith("/blob"):
            self.reply(200, BLOB, "application/octet-stream")
        elif self.path.startswith("/text"):
            self.reply(200, b"not json", "text/plain")
        elif self.path.startswith("/missing"):
            self.reply(404, b'{"error": "missing"}')
        else:
            echo = {"method": self.command, "path": self.path, "body": body, "cookie": self.headers.get("Cookie", "")}
            self.reply(200, json.dumps(echo).encode())

    do_GET = do_POST = do_PUT = do_DELETE = answer


@pytest.fixture
def server() -> Iterator[str]:
    httpd = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    yield f"http://127.0.0.1:{httpd.server_address[1]}"
    httpd.shutdown()
    httpd.server_close()


def test_facade_options_are_keyword_only() -> None:
    options: dict[Callable[..., object], str] = {
        net.get_json: "cookies",
        net.post_json: "cookies",
        net.put_json: "cookies",
        net.delete_json: "cookies",
        files.entries: "depth",
        files.archive: "level",
    }
    for function, name in options.items():
        assert inspect.signature(function).parameters[name].kind is inspect.Parameter.KEYWORD_ONLY, function


def test_json_requests(server: str) -> None:
    got = rn.request_json(rn.METHOD_GET, server + "/api", {"q": "a b", "n": "1", "skip": 5})
    assert got["method"] == "GET" and got["path"] == "/api?q=a%20b&n=1"  # Rack sends string values only
    posted = net.post_json(server + "/api", {"x": [1, 2]}, cookies={"token": "abc"})
    assert isinstance(posted, dict) and isinstance(posted["body"], str)
    assert (
        posted["method"] == "POST" and json.loads(posted["body"]) == {"x": [1, 2]} and posted["cookie"] == "token=abc;"
    )
    with pytest.raises(TypeError, match="cannot become JSON"):
        net.post_json(server + "/api", object())
    with pytest.raises(NotImplementedError, match="cannot safely send"):
        net.put_json(server + "/api", {"y": "z"})
    # These names are distinct in Rack's enum even though its curl PUT path sends POST.
    assert rn.Method.METHOD_PUT.value == 2 and rn.METHOD_PUT is rn.Method.METHOD_PUT
    # Rack 2.6.6 sets CURLOPT_POSTFIELDS after CURLOPT_UPLOAD, causing curl to POST.
    raw_put = rn.request_json(rn.Method.METHOD_PUT, server + "/api", [1], {})
    assert raw_put["method"] == "POST" and json.loads(raw_put["body"]) == [1]
    deleted = net.delete_json(server + "/api")
    assert isinstance(deleted, dict) and deleted["method"] == "DELETE"
    assert net.get_json(server + "/text") is None  # not JSON
    assert net.get_json("http://127.0.0.1:1/") is None  # nothing listens: refused at once
    assert net.get_json("not a url") is None


def test_downloads(server: str, tmp_path: Path) -> None:
    target = tmp_path / "blob.bin"
    seen: list[float] = []
    assert net.download(server + "/blob", target, seen.append, interval=0.001)
    assert target.read_bytes() == BLOB and seen[-1] == 1.0 and all(0.0 <= v <= 1.0 for v in seen)
    assert not rn.request_download(server + "/missing", tmp_path / "m.bin")  # an HTTP error fails
    progress = rn.DownloadProgress()
    assert rn.request_download(server + "/blob", tmp_path / "b2.bin", progress) and progress.value == 1.0


def test_url_helpers() -> None:
    assert rn.encode_url("a b&c=d") == "a%20b%26c%3Dd"
    assert rn.url_path("https://example.com/foo/index.html") == "/foo/index.html"
