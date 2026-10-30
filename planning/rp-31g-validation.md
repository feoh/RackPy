# RP-31G validation

Plain `rack::Quantity` and `ValueQuantity` subclasses can override the five
value/range methods on the main thread. `ParamQuantity` and `SwitchQuantity`
subclasses still reject those overrides outside the callback laboratory.
Off-main-thread plain-quantity calls use the native base, without entering
Python, and emit at most one refusal diagnostic per object. This fallback is
not semantically equivalent to a Python override and is not a production DSP
path. The callback matrix now distinguishes the plain UI methods from
`ParamQuantity`'s engine-reachable mixed methods.

Linux x86_64, Rack Free 2.6.6:

- `uv run bin/rackpy-dev build --werror --test-hooks` and
  `uv run bin/rackpy-dev build --build-dir build/production --werror`: passed,
  each with 6/6 CTests.
- `uv run pytest -q tests/values/test_values_in_rack.py tests/ui/test_ui.py tests/laboratory/test_laboratory.py`:
  5 passed, with temporary fixture overrides pointing to those builds, removed
  afterwards. Both production and test-hooks value/laboratory suites ran. A
  subsequent rerun of values and UI after strengthening the bounds assertions
  passed (3 passed). In actual Rack, a Slider invokes Python value methods,
  `ParamQuantity` rejects overrides, a Rack thread receives a native fallback
  and one diagnostic, and `ValueQuantity` subclass `super()` works.
- Mutation: temporarily changed the main-thread dispatch decision to `false`,
  rebuilt, and ran `uv run pytest -q tests/ui/test_ui.py` with the hooks-build
  override. The host suite failed specifically at
  `test_slider_dispatches_plain_quantity_value_overrides`; the source was
  restored and the normal build and host suite passed afterward.
- `uv run pytest -m unit -q`: 106 passed, 203 deselected.
- `uv run pytest -q tests/coverage/test_introspection.py`: 3 passed, using a
  temporary override of `dev_build` to the production build, then restored.
- `uv run prek run --all-files`, `uv run bin/rackpy-dev typecheck`,
  `uv run bin/generate_bindings.py check`, and
  `uv run sphinx-build -W -n -b html docs build/docs/html`: passed.

macOS and Windows were not built or run. No coverage status was promoted to
`verified`, and no performance claim is made.
