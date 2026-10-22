# SPDX-License-Identifier: GPL-3.0-or-later
"""Sphinx configuration for the RackPy documentation site (RP-09, ADR 006).

Build: uv run sphinx-build -W --keep-going -n -b html docs build/docs/html
Nothing here imports rackpy or needs Rack: API pages come from sphinx-autoapi,
which parses src/, and SDK tables come from upstream/api-surface.json.
"""

import sys
from pathlib import Path

DOCS = Path(__file__).resolve().parent
sys.path.insert(0, str(DOCS / "_ext"))

from rackpy_docs import REPO, repo_facts  # noqa: E402  (needs the path above)

facts = repo_facts()

project = "RackPy"
author = "RackPy contributors"
project_copyright = "RackPy contributors; GPL-3.0-or-later"
release = facts["rackpy_version"]
version = release
language = "en"

extensions = ["myst_parser", "autoapi.extension", "rackpy_docs"]
root_doc = "index"
exclude_patterns = ["_build", "_ext", "snippets", "Thumbs.db", ".DS_Store"]
nitpicky = True
# Warnings are errors (sphinx-build -W); these are the only reviewed exceptions.
# autoapi cannot follow a .pyi stub that imports from its own package's __init__.pyi
# (rackpy.raw._types -> rackpy.raw.Quantity); the class is documented where it is defined.
suppress_warnings = ["autoapi.python_import_resolution"]
nitpick_ignore_regex = [
    # The build is offline (no intersphinx): standard-library types in signatures cannot resolve.
    ("py:class", r"(collections\.abc|typing|types|array)\.\w+"),
    # PEP 695 type parameters (def f[T](...)) are not documented objects.
    ("py:class", r"[A-Z]"),
    # Private implementation modules; their public names are documented where rackpy.raw re-exports them.
    ("py:class", r"rackpy\.raw\._\w+\.\w+"),
]

# --- Markdown ---
myst_heading_anchors = 4
myst_enable_extensions = ["substitution", "colon_fence"]
myst_substitutions = facts

# --- Python API, from source without importing it ---
autoapi_dirs = [str(REPO / "src")]
autoapi_root = "reference/python"
autoapi_add_toctree_entry = False
autoapi_keep_files = False
# imported-members: the facade modules (rackpy.math, rackpy.types) are curated re-exports.
autoapi_options = ["members", "undoc-members", "show-module-summary", "imported-members"]
autoapi_python_class_content = "both"

# --- HTML ---
html_theme = "furo"
html_title = f"RackPy {release}"
html_static_path: list[str] = []
html_last_updated_fmt = None
html_show_sourcelink = False
html_theme_options = {
    "source_repository": "https://github.com/feoh/RackPy/",
    "source_branch": "main",
    "source_directory": "docs/",
    "announcement": "RackPy is in early development. No Rack API is bound yet, and there is no release.",
}

# --- External link check (sphinx-build -b linkcheck), bounded ---
linkcheck_timeout = 20
linkcheck_retries = 2
linkcheck_workers = 4
linkcheck_rate_limit_timeout = 60.0
linkcheck_anchors = False  # GitHub renders anchors with JavaScript
linkcheck_ignore = [
    # Thousands of line links into the pinned Rack commit, from the SDK tables.
    # One of them is checked through the compatibility page's commit link.
    r"https://github\.com/VCVRack/Rack/blob/[0-9a-f]{40}/include/.*#L\d+",
    # Links to this repository at the commit being built, which is not pushed yet during local builds.
    r"https://github\.com/feoh/RackPy/(blob|tree)/[0-9a-f]{40}/.*",
]
