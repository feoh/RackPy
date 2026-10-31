# Security policy

## Status

RackPy is unreleased. There is no supported version yet: only the `main`
branch, built from source on Linux x86_64. Security fixes go to `main`. Once
there is a release, this page will say which releases receive them.

## RackPy is not a sandbox

A RackPy plugin's Python has the same power as a C++ plugin: it runs with
your user account's rights, inside Rack, and can read and write your files,
use the network and start programs. **Install only plugins you trust**, as
for any other Rack plugin. RackPy does not make untrusted Python safe to run,
and no part of it is meant to.

What RackPy does guarantee is that it runs **only the Python files shipped
in the plugin**, and its bundled standard library. In particular:

- patch files, a module's saved data and its patch storage are data: no
  patch can make RackPy run code, unpickle anything or import a file;
- Python environment variables (`PYTHONPATH`, `PYTHONHOME`,
  `PYTHONSTARTUP`, ...), user site-packages, `.pth` and `sitecustomize`
  files, and the working directory are ignored;
- a plugin file named like a standard module never replaces it;
- the loader refuses a configuration, host library or Python path that
  resolves outside the plugin folder, and refuses to share the process with
  another Python runtime.

[Security and trust](docs/explanation/security.md) explains this model, its
tests and its limits, including the risks of distributing a plugin that
bundles a Python runtime.

## Reporting a vulnerability

Report it **privately**: please do not open a public issue with details of a
vulnerability.

Use GitHub's private vulnerability reporting for this repository
(**Security → Report a vulnerability** at <https://github.com/feoh/rackpy>).
It is not enabled yet; enabling it is required before the first release. Until
then, open an issue that asks for a private contact and contains no details,
and the maintainer will reply with one.

Please include what you ran (the RackPy commit, Rack version and operating
system), what happened, and what you expected. A fix and its disclosure are
coordinated with you.

What counts as a vulnerability here: anything that makes RackPy run code it
should not (from a patch, the environment or another folder), memory
corruption reachable from a patch or from correct use of the API, and Python
reaching Rack's audio threads. A plugin's own Python doing something harmful
is not a RackPy vulnerability: that plugin is trusted code by design.
