# Changelog

## 0.1.0.dev1 — local Linux preview; not published

This is the first owner-approved local preview version. Its artifact version
includes `+g<commit>` so the build identifies its exact source. It is intended
for local testing on Linux x86_64 with Rack Free 2.6.6 only; it is not a public
release or a claim of complete API coverage, broad platform support, or
performance certification. Rack plugin metadata in the `.vcvplugin` examples
continues to say `2.0.0`; that is Rack's plugin version field, not RackPy's
package version.

- Built the Linux x86_64 candidate from commit
  `52526e83c0c0bfc74d223e46ea78319450336766`; its manifest and checksums are
  retained under [`release-evidence/candidate-linux-x64/`](release-evidence/candidate-linux-x64/).
- Verified all 13 artifact checksums and installed/ran all ten example plugins
  in a clean Ubuntu 20.04 container with no system Python or network access.
  Patch reopen, windowed smokes, upgrade/uninstall, and error-case checks passed.
  The same checks subsequently passed in Ubuntu 16.04. The owner also reported
  hearing the oscillator in a manual Rack Pro 2.6.6 smoke; this is not a broad
  compatibility or performance claim. See the
  [`candidate audit`](release-evidence/candidate-audit.md).
- Documented Linux production audio-path/lifecycle safety evidence and its
  limitations in [`docs/performance-results.md`](docs/performance-results.md).
- Coverage remains incomplete and explicitly partial. Controlled performance
  measurement and a physical audio deadline test remain outstanding. No
  artifacts have been published; publication requires explicit owner approval.
