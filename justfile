set windows-shell := ["sh", "-cu"]

builddir := "build"

# clang-tidy must match the build toolchain, or cglm's SIMD headers throw
# hundreds of bogus parse errors. On Windows that means the active MSYS2
# environment's binary (via $MINGW_PREFIX: clang64, ucrt64, ...), not a stray
# Program Files LLVM; on Linux the clang-tidy on PATH is already correct.
clang_tidy := if os() == "windows" {
    env_var_or_default("MINGW_PREFIX", "C:/msys64/clang64") + "/bin/clang-tidy.exe"
} else {
    "clang-tidy"
}

# run-clang-tidy resolves paths to absolute, so the positional filter matches the
# absolute path segment; '.' stands in for the path separator. This excludes
# subproject sources (libsixel, cglm) while keeping our own src/ tree.
tidy_filter := "dcat.src."

default:
    @just --list

# Configure a release build. Extra meson-setup args pass through (e.g. -Dfoo=bar); override the tree with `just builddir=other setup-release`.
setup-release *args:
    meson setup {{builddir}} --buildtype=release --reconfigure {{args}}

# Configure a debug build (asserts, Vulkan validation). Same extra-arg passthrough as setup-release.
setup-debug *args:
    meson setup {{builddir}} --buildtype=debug --reconfigure {{args}}

# Configure a debug build instrumented with AddressSanitizer + UBSan.
# Uses Meson's built-in b_sanitize; b_lundef=false keeps sanitizer runtime linking happy.
# NOTE: ASan is unusable on native Windows here -- the prebuilt libvips/glib DLL
# allocates pointers ASan never tracks, so vips_init aborts with an unsuppressible
# bad-malloc_usable_size. Use this on Linux; on Windows use `just ubsan`.
asan:
    meson setup {{builddir}} --buildtype=debug -Db_sanitize=address,undefined -Db_lundef=false --reconfigure
    meson compile -C {{builddir}}

# Configure a debug build instrumented with UBSan only. Works on native Windows,
# where ASan cannot be used (see the asan recipe note).
ubsan:
    meson setup {{builddir}} --buildtype=debug -Db_sanitize=undefined -Db_lundef=false --reconfigure
    meson compile -C {{builddir}}

# Build whatever is currently configured. Extra meson-compile args pass through (e.g. -v).
build *args:
    meson compile -C {{builddir}} {{args}}

# Run the unit tests (built by default when the Unity wrap is available).
test:
    meson test -C {{builddir}} --print-errorlogs

# Report clang-tidy findings without changing anything.
tidy:
    run-clang-tidy -p {{builddir}} -clang-tidy-binary "{{clang_tidy}}" -header-filter='dcat.src' '{{tidy_filter}}'

# Apply clang-tidy fixes, then reformat (fixes are applied unformatted).
tidy-fix:
    run-clang-tidy -p {{builddir}} -clang-tidy-binary "{{clang_tidy}}" -fix -header-filter='dcat.src' '{{tidy_filter}}'
    just fmt

# Format all C sources and headers under src/ and tests/ with .clang-format.
# Pass the file list straight to clang-format (no xargs; msys2 sh lacks it).
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

# Cut a release: bump the project() version, commit, tag vX.Y.Z, and push both.
release version:
    sed -i "s/^  version: '[^']*',/  version: '{{version}}',/" meson.build
    git add meson.build
    git commit -m "Release v{{version}}"
    git tag "v{{version}}"
    git push origin HEAD
    git push origin "v{{version}}"
