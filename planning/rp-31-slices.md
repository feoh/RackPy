# RP-31 bounded implementation slices

RP-31 (`tk-rackpy-31-reconcile-all-transitive-families-and--2b212f`)
reconciles every family against the inventory and closes the coverage gaps
an audit finds. These child contracts supplement, not replace,
[RP-31 in TASKS.md](TASKS.md); live Witan status is authoritative.

## Audit (2026-10-27)

- **Fresh inventory.** `bin/inventory_sdk.py check` re-extracted both Linux
  variants: all 3,095 entries are identical to the committed inventory; only
  the system libclang changed (23.1.3 to 23.1.4). Windows and macOS
  extraction still fails for want of those targets' C++ headers
  (`'cstddef' file not found`).
- **Evidence heuristics** over the 2,945 implemented records flagged those
  whose cited tests' directory trees never mention the Python name, and
  names that appear on no docs page; seven read-only auditors then checked
  every candidate by hand, five per family (records, verdicts and file:line
  evidence in [rp-31-audit/evidence.json](rp-31-audit/evidence.json)) and
  two qualitatively ([overloads, templates, macros and deprecations](rp-31-audit/qualitative-overloads-templates-macros.md);
  [external adapters and transitive types](rp-31-audit/qualitative-adapters-transitive.md)).
  Weak or missing test evidence: about 360 records; missing docs: about 150.
  Every overloaded name the heuristic flagged was found reachable and
  exercised except three overloads (RP-31C).
- **Mutation tests** (`tests/coverage/test_mutations.py`, unit): on the real
  manifest and inventory, removing overload records, dropping overloaded
  names from the built package's exports, unmapping callbacks or giving them
  a wrong strategy, and turning a callback into a blocker in the generated
  inventory each fail the checks for exactly those declarations.

## Decisions

- **Windows and macOS inventories** are deferred to RP-40 and RP-41 (owner,
  2026-10-27); they stay unresolved and keep a release on those platforms
  blocked.
- **Engine-thread audio and MIDI I/O** for Python plugins gets native
  graph/kernel nodes (owner, 2026-10-27): RP-31J.
- **Template contracts**: the documented instantiation sets are accepted,
  and the cheap gaps closed (owner, 2026-10-27): RP-31K.
- **Weak pointers and locks**: the usable contracts are documented, and
  `SharedLock` accepts any object with `lock_shared`/`unlock_shared` (owner,
  2026-10-27): RP-31K.

## Slices

Each blocks RP-31; RP-31 closes only after all of them, with a final full
manifest audit and an independent review focused on scope cheats.

| Slice | Witan | Scope |
| --- | --- | --- |
| RP-31A | `tk-rackpy-31a-evidence-gaps-in-app-widgets-and-the--a09bd8` | evidence: app widgets and component library |
| RP-31B | `tk-rackpy-31b-evidence-gaps-in-widgets-ui-events-an-e785b5` | evidence: widgets, UI, events, window |
| RP-31C | `tk-rackpy-31c-evidence-gaps-in-dsp-simd-and-math-d51215` | evidence: DSP, SIMD, math; three unexercised overloads |
| RP-31D | `tk-rackpy-31d-evidence-gaps-in-engine-plugin-histor-647588` | evidence: engine, plugin, history, patch, random, context |
| RP-31E | `tk-rackpy-31e-evidence-gaps-in-settings-services-an-069da1` | evidence: settings (native round trip), services, audio/MIDI I/O |
| RP-31F | `tk-rackpy-31f-deprecation-warnings-for-every-deprec-7b7ab1` | seven deprecated declarations that never warn; a checker rule |
| RP-31G | `tk-rackpy-31g-python-overrides-of-plain-rack-quanti-73ed52` | plain `Quantity` value overrides on the UI thread |
| RP-31H | `tk-rackpy-31h-glfw-input-constants-and-blendish-cor-36e9df` | GLFW input constants and `BND_CORNER_*` as boundary entries |
| RP-31I | `tk-rackpy-31i-records-for-declarations-on-preproces-b14e77` | records for declarations on skipped preprocessor branches |
| RP-31J | `tk-rackpy-31j-native-production-i-o-paths-for-audio-8ba798` | native audio-port and MIDI-queue nodes for kernels and graphs |
| RP-31K | `tk-rackpy-31k-settle-template-weak-pointer-and-lock-e4976c` | template, weak-pointer and lock contracts; an owner-review gate |
| RP-31L | `tk-rackpy-31l-note-typing-and-small-test-cleanups-f-df6aa2` | notes, container typing and small test gaps |
| RP-31M | `tk-rackpy-31m-faithfully-map-and-prove-plugin-destr-37b2a4` | Plugin destructor and attached-model lifetime semantics; blocks RP-31D |
