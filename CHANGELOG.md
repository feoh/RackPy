# Changelog

## 0.1.0.dev1 — local Linux preview; not published

This is the first owner-approved local preview version. Its artifact version
includes `+g<commit>` so the build identifies its exact source. It is intended
for local testing on Linux x86_64 with Rack Free 2.6.6 only; it is not a public
release or a claim of complete API coverage, broad platform support, or
performance certification. Rack plugin metadata in the `.vcvplugin` examples
continues to say `2.0.0`; that is Rack's plugin version field, not RackPy's
package version.

- Built a Linux x86_64 candidate from commit
  `6a62604a0dc81bd32dd0e16ce564813852847072`; its checksums and artifact
  manifest are retained under [`release-evidence/candidate-linux-x64/`](release-evidence/candidate-linux-x64/).
- Installed and ran the ten example plugins in an isolated Ubuntu 20.04
  environment with no system Python or network access; upgrade, uninstall,
  patch reopening, and windowed smoke checks passed.
- Documented Linux production audio-path/lifecycle safety evidence and its
  limitations in [`docs/performance-results.md`](docs/performance-results.md).
- Coverage remains incomplete and explicitly partial. Controlled performance
  measurement and a physical audio deadline test remain outstanding. No
  artifacts have been published; publication requires explicit owner approval.
