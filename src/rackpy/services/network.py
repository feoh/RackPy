# SPDX-License-Identifier: GPL-3.0-or-later
"""HTTP, over :mod:`rackpy.raw.network`.

Rack's requests block the calling thread (with the GIL released) in curl, which allows 30 seconds
to connect and sets no overall limit. Any failure (no connection, an HTTP error, a body that is
not JSON) returns ``None``, as Rack does. Never call them from code that must not wait.
"""

import os
from collections.abc import Callable
from typing import cast

from rackpy.raw import network as _network
from rackpy.types import JSON

__all__ = ["delete_json", "download", "encode_url", "get_json", "post_json", "put_json", "url_path"]

encode_url = _network.encode_url
url_path = _network.url_path


def get_json(url: str, params: dict[str, str] | None = None, *, cookies: dict[str, str] | None = None) -> JSON:
    """GET a JSON API; ``params`` become the query (string values only, as Rack does). None on failure."""
    return cast(JSON, _network.request_json(_network.METHOD_GET, url, params, cookies or {}))


def post_json(url: str, data: object = None, *, cookies: dict[str, str] | None = None) -> JSON:
    """POST ``data`` as a JSON body. RackPy validates the value at runtime; None on failure."""
    return cast(JSON, _network.request_json(_network.METHOD_POST, url, data, cookies or {}))


def put_json(url: str, data: object = None, *, cookies: dict[str, str] | None = None) -> JSON:
    """Refuse to send: Rack 2.6.6's PUT with a JSON body actually sends POST."""
    raise NotImplementedError("Rack 2.6.6 cannot safely send a JSON PUT request")


def delete_json(url: str, *, cookies: dict[str, str] | None = None) -> JSON:
    """DELETE. None on failure."""
    return cast(JSON, _network.request_json(_network.METHOD_DELETE, url, None, cookies or {}))


def download(
    url: str,
    filename: str | os.PathLike[str],
    on_progress: Callable[[float], object] | None = None,
    *,
    cookies: dict[str, str] | None = None,
) -> bool:
    """Download to ``filename`` and return whether it succeeded.

    Rack writes its progress pointer without synchronization. RackPy therefore reads it only after the request
    returns and calls ``on_progress`` once with the final fraction; live progress is unavailable. If the callback
    raises, the download has already finished and the error propagates.
    """
    if on_progress is None:
        return _network.request_download(url, filename, None, cookies or {})
    progress = _network.DownloadProgress()
    result = _network.request_download(url, filename, progress, cookies or {})
    on_progress(progress.value)
    return result
