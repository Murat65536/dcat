builddir := "build"

# clang-tidy must match the build toolchain (msys2 clang64), not Program Files
# LLVM — otherwise cglm's SIMD headers throw hundreds of bogus parse errors.
clang_tidy := "C:/msys64/clang64/bin/clang-tidy.exe"

# run-clang-tidy resolves paths to absolute, so the positional filter matches the
# absolute path segment; '.' stands in for the path separator. This excludes
# subproject sources (libsixel, cglm) while keeping our own src/ tree.
tidy_filter := "dcat.src."

default:
    @just --list

# Configure an optimized release build.
setup-release:
    meson setup {{builddir}} --buildtype=release --reconfigure

# Configure a debug build (asserts, Vulkan validation).
setup-debug:
    meson setup {{builddir}} --buildtype=debug --reconfigure

# Configure a debug build instrumented with AddressSanitizer + UBSan.
# Uses Meson's built-in b_sanitize; b_lundef=false keeps sanitizer runtime linking happy.
asan:
    meson setup {{builddir}} --buildtype=debug -Db_sanitize=address,undefined -Db_lundef=false --reconfigure
    meson compile -C {{builddir}}

# Build whatever is currently configured.
build:
    meson compile -C {{builddir}}

# Report clang-tidy findings without changing anything.
tidy:
    run-clang-tidy -p {{builddir}} -clang-tidy-binary "{{clang_tidy}}" -header-filter='dcat.src' '{{tidy_filter}}'

# Apply clang-tidy fixes, then reformat (fixes are applied unformatted).
tidy-fix:
    run-clang-tidy -p {{builddir}} -clang-tidy-binary "{{clang_tidy}}" -fix -header-filter='dcat.src' '{{tidy_filter}}'
    just fmt

# Format all C sources and headers under src/ with .clang-format.
fmt:
    git ls-files -z -- 'src/*.c' 'src/*.h' | xargs -0 clang-format -i

# Drop into an environment with the built binary on PATH.
devenv:
    meson devenv -C {{builddir}}

# Remove the build directory.
clean:
    rm -rf {{builddir}}

# Refresh pinned subproject sources after editing subprojects/*.wrap.
bump-wraps:
    meson subprojects update --reset
