# VCMI Dependencies

This repository contains prebuilt Conan dependencies for VCMI. These dependencies are primarily used by GitHub Actions CI and can be used to avoid building dependencies locally by developers.

Current flow to update dependencies:
1. Open PR with changes
2. Make sure that CI build succeeds
3. Merge the PR
4. Run workflow to create new release from the CI run
5. (TBD) Update dependencies submodule and prebuilts URL in VCMI repo to point to the new commit / release, update VCMI code if needed

## Building locally

One can also build all the dependencies locally the same way as CI does by running Bash script `build.sh`. On Windows you can use Git Bash for instance.

Prerequisites: Python 3, Conan, CMake, Ninja - all of them must be accessible in the shell, i.e. their containing directories must be in the `PATH` environment variable.

Platform-specific preparations:
- if you're on Windows, you must install [Strawberry Perl](https://strawberryperl.com/) (required to build Qt 5) and set `WINDOWS_PERL_DIR` environment variable pointing to a directory containing `perl.exe`, for example: `export WINDOWS_PERL_DIR='/c/Program Files/Strawberry Perl/bin'`
- if building for Windows, you must install the latest MSVC **v142** toolset
- if you're on Linux and want to build for Android 32-bit, you must install `libc6-dev-i386` package (required to build LuaJIT), for example: `sudo apt install libc6-dev-i386`

Run the script with `bash build.sh` (or `./build.sh`) and pass the desired platform (*host* platform in Conan terms) as first parameter or in `BUILD_PLATFORM` environment variable, for example: `bash build.sh android-arm64`

You can find the list of supported platforms inside the [script](build.sh) right under the `# actual script` line.

For advanced use cases you can specify additional environment variables instead of the desired platform, please read the script starting from the `# actual script` line.

## TODO List

- Find a way to set up an artifactory on our server or find somebody willing to host it for us and deprecate this repository. Potential options:
  - Artifactory Community Edition (requires more high-spec server than our current one)
  - https://docs.conan.io/2/reference/conan_server.html (not recommended by Conan)
  - https://jfrog.com/

- Run CI with full package rebuild on schedule (weekly? monthly?) to detect any regressions or breaking changes in CI or in used recipes
