# Absolute sh.exe path + /usr/bin PATH prepend so recipes (and coreutils) work from PowerShell/cmd, where MSYS2 is not on PATH; settings are const, so this can't probe env.
set windows-shell := ["C:/msys64/usr/bin/sh.exe", "-cu", 'PATH="/usr/bin:$PATH"; eval "$0"']

builddir := "build"

# clang-tidy must match the build toolchain (on Windows: the active MSYS2 env's binary via $MINGW_PREFIX, not a stray Program Files LLVM), or cglm's SIMD headers throw bogus parse errors.
clang_tidy := if os() == "windows" {
    env_var_or_default("MINGW_PREFIX", "C:/msys64/clang64") + "/bin/clang-tidy.exe"
} else {
    "clang-tidy"
}

tidy_filter := "dcat.src."

default:
    @just --list

# Configure a release build. Extra meson-setup args pass through. Override the tree with `just builddir=other setup-release`.
setup-release *args:
    meson setup {{builddir}} --buildtype=release --reconfigure {{args}}

# Configure a debug build (asserts, Vulkan validation). Same extra-arg passthrough as setup-release.
setup-debug *args:
    meson setup {{builddir}} --buildtype=debug --reconfigure {{args}}

# Configure a debug build with ASan + UBSan (b_lundef=false for sanitizer runtime linking). Linux-only: the prebuilt libvips/glib DLL makes vips_init abort under ASan; on Windows use `just ubsan`.
asan:
    meson setup {{builddir}} --buildtype=debug -Db_sanitize=address,undefined -Db_lundef=false --reconfigure
    meson compile -C {{builddir}}

# Configure a debug build instrumented with UBSan only; works on native Windows, where ASan cannot be used (see the asan recipe note).
ubsan:
    meson setup {{builddir}} --buildtype=debug -Db_sanitize=undefined -Db_lundef=false --reconfigure
    meson compile -C {{builddir}}

# Build whatever is currently configured. Extra meson-compile args pass through (e.g. -v).
build *args:
    meson compile -C {{builddir}} {{args}}

# Run the unit tests (built by default when the Unity wrap is available).
test:
    meson test -C {{builddir}} --print-errorlogs

# Rebuild with coverage, run the tests, and print a gcovr summary (run from builddir on our object dirs, not via meson's coverage-text: llvm-cov can't resolve ../src/... from the root). Requires gcovr.
coverage:
    meson setup {{builddir}} --buildtype=debug -Db_coverage=true --reconfigure
    meson compile -C {{builddir}}
    meson test -C {{builddir}} --print-errorlogs
    cd {{builddir}} && gcovr -r .. libdcat_core.a.p tests --txt

# Report clang-tidy findings without changing anything.
tidy:
    run-clang-tidy -p {{builddir}} -clang-tidy-binary "{{clang_tidy}}" -header-filter='dcat.src' '{{tidy_filter}}'

# Apply clang-tidy fixes, then reformat (fixes are applied unformatted).
tidy-fix:
    run-clang-tidy -p {{builddir}} -clang-tidy-binary "{{clang_tidy}}" -fix -header-filter='dcat.src' '{{tidy_filter}}'
    just fmt

# Format all C sources and headers under src/ and tests/ with .clang-format, passing the file list straight to clang-format (no xargs; msys2 sh lacks it).
fmt:
    clang-format -i `git ls-files -- 'src/*.c' 'src/*.h' 'tests/*.c'`

# Drop into an environment with the built binary on PATH.
devenv:
    meson devenv -C {{builddir}}

# Remove the build directory.
clean:
    rm -rf {{builddir}}

# Refresh pinned subproject sources after editing subprojects/*.wrap.
bump-wraps:
    meson subprojects update --reset
