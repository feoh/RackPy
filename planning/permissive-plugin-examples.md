# Permissively licensed Rack plugins: RackPy example candidates

**Research snapshot: 2026-11-05.** This is a shortlist, not legal advice, an
implementation, or permission from the upstream authors beyond the licenses
identified below. No upstream author was contacted and no code, artwork, audio,
font, or binary was copied into RackPy.

## Recommendation

Use **Amplified Futures** as the first source to adapt, starting with the
behavior of its **Collapse EG** module. Keep **Feedback Governor** as the next
candidate if the first port establishes a useful RackPy DSP-example pattern.
Both are small, distinct workflows absent from RackPy's current ten examples;
both selected C++ files carry an MIT SPDX header, and the repository's pinned
MIT license explicitly covers its source. Implement new RackPy code and original
panels; retain attribution and the MIT notice for any portions actually derived.
Do not bring over the project's insect recordings, branding, artwork, or panel
layouts.

The candidate repository's VCV manifest sets plugin `.version` to `2.3.1`
(major version 2, indicating Rack 2) and `.license` to `MIT`, while RackPy's
pinned baseline is Rack 2.6.6. This is a reasonable compatibility signal,
**not a Rack 2.6.6 build or runtime test**.
RackPy's current release target is Linux x86_64 / Rack Free 2.6.6. Its published
examples already cover oscillator, configuration, state/events, menus, panels,
drawing, polyphony, DSP, and expanders; they do not yet include a standalone
AD envelope or a controlled audio-feedback return.

## Selection and reuse rules

- Rank examples by a useful workflow not already represented in
  [`examples/README.md`](../examples/README.md), a source license checked at an
  immutable ref, fit to the Rack 2.6.6 baseline, and whether the example can be
  made without third-party assets.
- RackPy is GPL-3.0-or-later. GNU's license list identifies MIT/Expat and
  modified BSD-3-Clause as GPL-compatible. When adapting code, retain the
  applicable upstream copyright/license/disclaimer text and add it to the
  example's notices; do not imply the upstream author endorses RackPy. VCV's
  plugin guidance separately says not to clone another product's brand, model
  name, logo, panel design, or component layout. RackPy's own guardrail also
  requires original panels.
- VCV's manifest manual says `.license` is required and recommends SPDX
  identifiers such as `MIT` and `BSD-3-Clause`. A manifest/SPDX label is a
  lead, not a substitute for reading the repository's actual license and
  checking file-level exceptions.
- Prior to implementation, validate selected source files and transitive
  dependencies at the pinned commit, run a Rack 2.6.6 build/host check, write
  independent tests, and verify attribution/third-party notices. No release
  or redistribution should be inferred from this research.

## Shortlist

### 1. Amplified Futures — Collapse EG (recommended first)

- **Upstream and immutable ref:** [repository](https://github.com/dboles99/amplified-futures-vcv),
  pinned snapshot [`06794bca8ccbc3b413b929713017a252783dce51`](https://github.com/dboles99/amplified-futures-vcv/tree/06794bca8ccbc3b413b929713017a252783dce51).
  The relevant files at that ref are [`src/CollapseEG.cpp`](https://github.com/dboles99/amplified-futures-vcv/blob/06794bca8ccbc3b413b929713017a252783dce51/src/CollapseEG.cpp),
  [`src/dsp/EnvCore.hpp`](https://github.com/dboles99/amplified-futures-vcv/blob/06794bca8ccbc3b413b929713017a252783dce51/src/dsp/EnvCore.hpp),
  and [`docs/modules/CollapseEG.md`](https://github.com/dboles99/amplified-futures-vcv/blob/06794bca8ccbc3b413b929713017a252783dce51/docs/modules/CollapseEG.md).
  The commit API reported this as the latest commit in the repository when
  checked; pin this exact SHA rather than the moving `master` branch.
- **License evidence and obligations:** the repository's
  [`LICENSE`](https://github.com/dboles99/amplified-futures-vcv/blob/06794bca8ccbc3b413b929713017a252783dce51/LICENSE)
  contains the full MIT grant, copyright notice, conditions, and disclaimer;
  the selected `.cpp` carries `Copyright (c) 2026 Daniel Boles` and
  `SPDX-License-Identifier: MIT`. `plugin.json` also says `MIT`. GitHub's
  repository metadata currently reports `NOASSERTION`/`other`; rely on the
  explicit license and file-level evidence, not the GitHub classifier alone.
  MIT requires preserving its copyright,
  permission notice, and disclaimer in source copies/substantial portions and
  reproducing them in binary distribution materials.
- **Behavior worth demonstrating:** a triggered attack/decay envelope, shaped
  curve, gate/trigger edge handling, inverted output, end-of-cycle pulse, and
  optional cycle looping. `MISFIRE` is an optional character feature and can
  be deferred. RackPy already binds Rack's `SchmittTrigger` and
  `PulseGenerator`; these map well to a small native DSP kernel and actual-Rack
  save/reopen tests. The candidate's source currently obtains the sample rate
  via `APP`; a RackPy port should use RackPy's supported context/sample-rate
  path, not copy raw global access. Keep all per-sample processing native.
- **Fit / proposed scope:** high. A new original RackPy `Envelope` example
  would cover a standard modular-synthesis workflow currently missing from the
  example inventory and exercise bound trigger/pulse helpers plus sample-rate
  handling. Don't reuse “Collapse EG,” “Amplified Futures,” or their panel.
- **Assets / effort / credit:** selected behavior needs no media. The upstream
  license explicitly says MIT covers source and panel artwork but excludes
  audio under `res/insects/`, which is InsectSet32-derived CC BY 4.0 audio with
  a required attribution file. Exclude that audio entirely; draw an original
  panel and use no upstream visuals. **Effort: medium**—one envelope kernel,
  focused DSP tests, and actual-Rack integration. If code is adapted, include
  Daniel Boles' MIT notice and record the pinned URL/SHA and the changes in
  `PROVENANCE.md`; if only behavior is independently reimplemented, credit it
  as the design reference without claiming copied code.

### 2. Amplified Futures — Feedback Governor (recommended second)

- **Upstream and immutable ref:** the same pinned repository and commit above;
  see [`src/FeedbackGovernor.cpp`](https://github.com/dboles99/amplified-futures-vcv/blob/06794bca8ccbc3b413b929713017a252783dce51/src/FeedbackGovernor.cpp)
  and [`docs/modules/FeedbackGovernor.md`](https://github.com/dboles99/amplified-futures-vcv/blob/06794bca8ccbc3b413b929713017a252783dce51/docs/modules/FeedbackGovernor.md).
- **License evidence and obligations:** same explicit MIT license and
  file-level SPDX/copyright header as Collapse EG. The insect recordings are
  not used by this module; do not include them in a RackPy example.
- **Behavior worth demonstrating:** a controlled feedback send/return with a
  level cap, tone low-pass, per-pass decay, DC blocker, CV control, and a
  momentary kill/reset path. It adds a real signal-loop example rather than a
  simple filter wrapper.
- **Fit / proposed scope:** high but more demanding. It complements RackPy's
  existing biquad example by illustrating recursive state and feedback safety.
  Implement an original native kernel; test bounded output, DC rejection,
  parameter/CV behavior, kill behavior, sample-rate changes, and the real Rack
  cable loop. Preserve Rack's once-per-sample ordering; do not batch across the
  loop or run Python on the production audio path.
- **Assets / effort / credit:** no third-party audio is needed; use an original
  panel and module name. **Effort: medium-high** because meaningful validation
  must include an actual Rack patch/cable loop, not just a standalone DSP test.
  Preserve the MIT notice for any adapted code and cite Daniel Boles, the
  repository, and the pinned commit in provenance.

### 3. SIM — Phi / Spike sequencer (conditional alternate)

- **Upstream and immutable ref:** [repository](https://github.com/imDanSable/SIM),
  pinned snapshot [`4658ae2c9a7a86fc9190a6a20cae28fc8af41206`](https://github.com/imDanSable/SIM/tree/4658ae2c9a7a86fc9190a6a20cae28fc8af41206).
  The plugin manifest at that ref sets `.version` to `2.1.2` (major version 2,
  indicating Rack 2) and `.license` to `BSD-3-Clause`; the root
  [`LICENSE.md`](https://github.com/imDanSable/SIM/blob/4658ae2c9a7a86fc9190a6a20cae28fc8af41206/LICENSE.md)
  contains the BSD 3-Clause notice and disclaimer.
- **Behavior worth demonstrating:** `Phi` sequences a polyphonic voltage buffer
  with next/reset/clock behavior; `Spike` is a gate sequencer with duration,
  delay, repetition and chance controls. These would add sequencing workflows
  not represented by the current RackPy examples, although RackPy already has
  an expander example.
- **License/assets and fit:** a direct code port is **not a clean first task**.
  `Phi.cpp` and `Spike.cpp` directly include `src/biexpander/biexpander.hpp`
  and related adapters. The repository separately licenses `slime4rack` under
  BSD-with-Attribution, requiring its copyright/license/disclaimer and the
  acknowledgment “This product includes software developed by Coriander V.
  Pines, doing business as Slime Child Audio (https://slimechildaudio.com/)”; its
  `sigslot` dependency is MIT and its DSEG font has a separate SIL OFL 1.1
  license/reserved font name. The root BSD license alone is not the whole
  notice inventory. Its expander-heavy behavior also overlaps more with
  `examples/expanders` than the two Amplified Futures candidates.
- **Effort / credit:** **high** for faithful behavior because it involves
  polyphonic buffers, multiple expander relationships, clock semantics, and
  host timing. Consider a new, simpler sequencer inspired by its behavior only
  after mapping every dependency and notice; do not copy its `biexpander`,
  panels, DSEG font, name, or component layout without separate review. Any BSD
  code reused needs the full Dan Sable BSD-3-Clause notice plus all applicable
  dependency notices; Slime Child Audio's required acknowledgment must be
  included if that library is reused.

## Not shortlisted for a first port

- **Amalgamated Harmonics** ([repository](https://github.com/jhoar/AmalgamatedHarmonics),
  snapshot [`bf74c2b7674c610cfc57421157d814c98bd92438`](https://github.com/jhoar/AmalgamatedHarmonics/tree/bf74c2b7674c610cfc57421157d814c98bd92438))
  has a standard BSD-3-Clause root license and interesting Scale Quantizer,
  Arpeggiator, and generative modules. However, the tree also carries
  `src/LICENSE-Fundamental.txt`, `src/dsp/Bogaudio-LICENSE.txt`, separately
  licensed fonts, and ComponentLibrary artwork. The selected `ScaleQuantiser`
  source calls shared `music::` helpers, so the full dependency/provenance and
  asset scope needs mapping before calling that module a clean BSD port. The
  repository's last commit in the API snapshot was in 2022; Rack 2.6.6
  compatibility has not been checked. Treat as a follow-up audit, not a current
  recommendation.
- **Neural Harmonics** ([repository](https://github.com/Neural-Harmonics/vcv-rack-plugin))
  exposes HTTP/MCP control of Rack. That is explicitly outside RackPy's
  in-process plugin/API scope, so it is not a suitable example even if its
  terms could be cleared.
- Fundamental itself is GPL-3.0-or-later, and its panel visual designs have
  separate restrictive terms. It is not a permissive-source candidate and
  RackPy must not clone its visuals or branding.

## Proposed next step

If approved, create an original RackPy envelope-generator example inspired by
Collapse EG's feature set, not a copy of its source or appearance. Use the
pinned Rack 2.6.6 baseline, the already-bound trigger/pulse helpers, native
per-sample processing, original artwork and parameter/model names. Add tests
for trigger edge semantics, curve endpoints, attack/decay timing, EOC pulse,
looping, sample-rate changes, patch save/reopen, and actual Rack loading. Keep
MIT attribution/notice if any upstream code is actually reused. Treat the
Feedback Governor as a separate later example, after the envelope workflow is
validated. This recommendation does not authorize implementation, contacting
upstream, or release publication; those need their own decision/task.

## Sources

- [RackPy example inventory](../examples/README.md) and [compatibility baseline](../docs/compatibility.md).
- [GNU license compatibility list](https://www.gnu.org/licenses/license-list.html) and [GPL FAQ](https://www.gnu.org/licenses/gpl-faq.en.html).
- [VCV Plugin Licensing](https://vcvrack.com/manual/PluginLicensing) and [Plugin Manifest](https://vcvrack.com/manual/Manifest).
- Repository files at pinned commits linked in each candidate section; immutable SHA URLs avoid moving-branch ambiguity.
