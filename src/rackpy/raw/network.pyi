# SPDX-License-Identifier: GPL-3.0-or-later
"""rack::network: HTTP requests, downloads and URLs."""

import enum
import os
from typing import Any

class Method(enum.IntEnum):
    METHOD_GET = 0
    METHOD_POST = 1
    METHOD_PUT = 2
    METHOD_DELETE = 3

METHOD_GET: Method
METHOD_POST: Method
METHOD_PUT: Method
METHOD_DELETE: Method
CookieMap = dict

def request_json(method: Method, url: str, data: Any = None, cookies: dict[str, str] = ...) -> Any: ...

class DownloadProgress:
    def __init__(self) -> None: ...
    @property
    def value(self) -> float: ...

def request_download(
    url: str, filename: str | os.PathLike[str], progress: DownloadProgress | None = None, cookies: dict[str, str] = ...
) -> bool: ...
def encode_url(s: str) -> str: ...
def url_path(url: str) -> str: ...
