# Reproducible RP-42 review baseline

## Frozen candidate

- Frozen candidate for this review/carry round: `2ddd3084724d8ac3898e9cee1fb0b89fd5920acc`
- Prior verdict source: `c7566a2a721bd7c15176ea01039bd8ed14a689e8`
- Carry calculation: **604 carried, 2,489 redo** (3,093 implemented manifest records total)
- Redo plan: `2ddd308/redo/slices.json` — 100 chunks, at most 25 records each
- All-implemented denominator check: `2ddd308/all-implemented-slices.json` — 3,093 records, 150 chunks
- Carry provenance: `carry-c7566a2a-2ddd308.json`

The 604 verdicts were carried only where RP-46 found the manifest entry, its cited tests, and mapping sources byte-identical between the two commits. The other 2,489 records require fresh review. This is not a full-coverage verdict and does not promote any record to `verified`.

## Fresh-review progress (2026-11-16)

The `review/` folder now contains exact-slice verdict files for all 100 redo chunks (2,489 records), structurally checked by `bin/release-evidence verdicts` against the frozen SHA and slice list. These verdicts have not been promoted. Four candidate findings remain: three manifest ownership mismatches for `MidiParser::aftertouches`, `gates`, and `modFilters` in `redo-027`, and missing typed-template exposure for `ModuleWidget::getModule<T>()` in `redo-087`. They are preserved as `finding` verdicts and block promotion of those records; fix in a later candidate and re-review is required before a full-coverage claim. `bin/check_coverage.py check` still reports that runtime-package introspection evidence was not supplied. The coverage report shows 0 `verified` records. Exact-SHA platform/sample evidence and other RP-42 release gates remain outstanding.

## Reproduce

From this checkout at commit `2ddd3084724d8ac3898e9cee1fb0b89fd5920acc` (the generated evidence is stored in the subsequent evidence commit):

```bash
uv run bin/release-evidence carry \
  c7566a2a721bd7c15176ea01039bd8ed14a689e8 \
  2ddd3084724d8ac3898e9cee1fb0b89fd5920acc \
  --dest release-evidence/review-baselines/carry-c7566a2a-2ddd308.json
uv run bin/release-evidence slices \
  --carry-plan release-evidence/review-baselines/carry-c7566a2a-2ddd308.json \
  --dest release-evidence/review-baselines/2ddd308/redo
```

The lost historical `4fba8b9` → `e329b10` count is waived by owner decision and is not used here. Exact-SHA, carry-provenance, and changed-record re-review safeguards remain in force. Fresh review of all 2,489 redo records is complete, with four findings still open. Until those findings are fixed in a later candidate and re-reviewed, exact-candidate platform/sample evidence is collected, and all remaining RP-42 gates pass, no completeness claim is permitted.
