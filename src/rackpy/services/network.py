# SPDX-License-Identifier: GPL-3.0-or-later
"""HTTP, over :mod:`rackpy.raw.network`.

Rack's requests block the calling thread (with the GIL released) in curl, which allows 30 seconds
to connect and sets no overall limit. Any failure (no connection, an HTTP error, a body that is
not JSON) returns ``None``, as Rack does. Never call them from code that must not wait.
"""

import os
import threading
from collections.abc import Callable
from typing import Any

from rackpy.raw import network as _network

__all__ = ["delete_json", "download", "encode_url", "get_json", "post_json", "put_json", "url_path"]

encode_url = _network.encode_url
url_path = _network.url_path


def get_json(url: str, params: dict[str, str] | None = None, cookies: dict[str, str] | None = None) -> Any:
    """GET a JSON API; ``params`` become the query (string values only, as Rack does). None on failure."""
    return _network.request_json(_network.METHOD_GET, url, params, cookies or {})


def post_json(url: str, data: Any = None, cookies: dict[str, str] | None = None) -> Any:
    """POST ``data`` as a JSON body. None on failure."""
    return _network.request_json(_network.METHOD_POST, url, data, cookies or {})


def put_json(url: str, data: Any = None, cookies: dict[str, str] | None = None) -> Any:
    """PUT ``data`` as a JSON body. None on failure."""
    return _network.request_json(_network.METHOD_PUT, url, data, cookies or {})


def delete_json(url: str, cookies: dict[str, str] | None = None) -> Any:
    """DELETE. None on failure."""
    return _network.request_json(_network.METHOD_DELETE, url, None, cookies or {})


def download(
    url: str,
    filename: str | os.PathLike[str],
    on_progress: Callable[[float], object] | None = None,
    *,
    interval: float = 0.1,
    cookies: dict[str, str] | None = None,
) -> bool:
    """Downloads to ``filename``; whether it succeeded.

    With ``on_progress``, Rack's download runs on a worker thread and ``on_progress(fraction)`` is
    called on this thread every ``interval`` seconds while it runs, and once more at the end.
    """
    if on_progress is None:
        return _network.request_download(url, filename, None, cookies or {})
    progress = _network.DownloadProgress()
    result: list[bool] = []
    worker = threading.Thread(
        target=lambda: result.append(_network.request_download(url, filename, progress, cookies or {}))
    )
    worker.start()
    while worker.is_alive():
        on_progress(progress.value)
        worker.join(interval)
    on_progress(progress.value)
    return bool(result and result[0])
