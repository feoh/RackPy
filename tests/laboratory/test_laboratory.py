# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-22: the callback laboratory, inside actual Rack.

The suite runs at plugin start-up in the production build (no Rack context:
a Bench needs none), and again with a live context in the test-hooks build,
where it also compares a Bench with Rack's own engine.
"""

from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import rack_platforms, run_pytest_in_rack

pytestmark = [pytest.mark.host, rack_platforms()]
SUITE = Path(__file__).resolve().parent / "in_rack"


# Tests that need the other run's conditions: Rack's live context, or none.
NEEDS_RACK = [
    "test_racks_to_json_calls_python_params_and_data_overrides",
    "test_racks_module_code_reaches_slot_quantities_python_value_methods",
    "test_rack_threads_never_run_laboratory_python",
    "test_rack_threads_never_run_midi_generator_python",
    "test_racks_output_reaches_a_python_output_device",
    "test_laboratory_midi_device_native_reentry_super_and_recursion_limit",
    "test_racks_audio_port_calls_a_python_driver_and_device",
    "test_audio_override_errors_reach_the_port_methods_caller",
    "test_audio_teardown_runs_no_python_and_leaves_ports_pointing_nowhere",
    "test_inconsistent_audio_overrides_leave_no_dangling_ports",
    "test_rack_threads_never_run_laboratory_audio_python",
    "test_racks_midi_ports_and_widgets_call_a_python_driver_and_devices",
    "test_a_laboratory_input_receives_messages_synchronously",
    "test_a_laboratory_input_queue_overrides_racks_queue",
    "test_midi_override_errors_reach_the_caller",
    "test_midi_teardown_runs_no_python_and_leaves_ports_pointing_nowhere",
    "test_inconsistent_midi_overrides_leave_no_dangling_ports",
    "test_rack_threads_never_run_laboratory_midi_python",
    "test_a_subscribe_override_that_removes_its_device_leaves_the_port_pointing_nowhere",
    "test_audio_driver_teardown_detaches_ports_before_its_devices_go",
    "test_subscriptions_cannot_change_during_a_synchronous_delivery",
    "test_objects_dropped_during_a_delivery_go_after_it",
    "test_a_delivery_that_raises_still_releases_what_it_kept",
    "test_midi_driver_teardown_detaches_ports_before_its_devices_go",
    "test_racks_own_menu_items_cannot_change_a_subscription_during_a_delivery",
    "test_a_delivery_started_while_a_laboratory_object_is_deallocated_does_not_resurrect_it",
    "test_releases_drained_after_a_keyboard_delivery_still_count_as_inside_it",
    "test_a_driver_an_override_drops_inside_racks_port_code_goes_after_it",
    "test_an_audio_driver_an_override_drops_inside_racks_port_code_goes_after_it",
]
NEEDS_NO_RACK = ["test_parameter_events_and_json_need_racks_context"]


def expect_only(outcomes: dict[str, str], skipped: list[str]) -> None:
    """Every test passed, except exactly the ones this run cannot provide for, which skipped."""
    assert outcomes, "no tests ran"
    expected = {n for n in outcomes if n.rsplit("::", 1)[-1] in skipped}
    assert len(expected) == len(skipped), sorted(expected)
    assert {n for n, o in outcomes.items() if o != "passed"} == expected
    assert {outcomes[n] for n in expected} <= {"skipped"}


UI_ONLY = ["test_engine_reference.py", "test_engine_events.py"]


def test_laboratory_in_production_host(make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    ignored = [arg for name in UI_ONLY for arg in ("--ignore", str(SUITE / name))]
    result = run_pytest_in_rack(make_bundle(), rack_host, tmp_path, SUITE, args=ignored)
    assert result.exit == 0, result.summary()
    expect_only(result.outcomes(), NEEDS_RACK)


@pytest.mark.gui
def test_laboratory_with_live_rack_context(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, SUITE, during_ui=True)
    assert result.exit == 0, result.summary()
    outcomes = result.outcomes()
    for ui_test in ("test_a_bench_reproduces_racks_engine_frame_for_frame", "test_bench_events_match_racks_engine"):
        assert any(ui_test in n for n in outcomes), result.summary()
    expect_only(outcomes, NEEDS_NO_RACK)
