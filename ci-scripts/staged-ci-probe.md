# Staged CI validation probe

This file intentionally changes a non-documentation path so the production CI
workflow runs without changing OpenToonz source code.

The accompanying temporary workflow validates orchestration behavior:

1. A superseded pull-request run is cancelled.
2. A Windows failure prevents Ubuntu, Fedora, and macOS from starting.
3. An Ubuntu failure occurs only after Windows succeeds and prevents Fedora and
   macOS from starting.
4. A successful Ubuntu matrix allows Fedora and macOS to start independently.
5. The production CI performs a complete staged OpenToonz build and produces
   the expected artifacts.

This probe PR is disposable and must not be merged.
