# Changelog

## Unreleased — partial development candidate; not published

No versioned RackPy release has been made. These notes describe current
candidate-validation evidence only; they do not imply complete API coverage,
platform release support, or performance certification.

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
