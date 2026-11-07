# SPDX-License-Identifier: GPL-3.0-or-later
"""Feedback Governor, an original mono feedback-loop example."""

from rackpy.plugin import plugin

from . import governor

plugin.add_model("FeedbackGovernor", governor.configure, widget=governor.FeedbackGovernorWidget)

FeedbackGovernorWidget = governor.FeedbackGovernorWidget
__all__ = ["FeedbackGovernorWidget"]
