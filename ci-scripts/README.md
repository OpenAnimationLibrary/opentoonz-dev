# OpenToonz CI layout

The coordinated GitHub Actions workflow is defined in `.github/workflows/ci.yml`.

Its staged dependency order is:

1. Windows build and portable artifact
2. Ubuntu GCC and Clang builds
3. Fedora compile check and macOS build in parallel

The Ubuntu GCC job creates the Linux AppImage artifact. The Clang and Fedora jobs are compile checks and do not repeat Linux packaging.

Pull-request runs cancel superseded runs when a newer commit is pushed. Branch pushes run the full workflow only on `main`, avoiding duplicate push and pull-request builds for feature branches.
