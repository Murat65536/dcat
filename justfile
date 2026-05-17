builddir := "build"

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

# Run the test suite (pytest CLI harness) with full error logs.
test:
    meson test -C {{builddir}} --print-errorlogs

# Drop into an environment with the built binary on PATH.
devenv:
    meson devenv -C {{builddir}}

# Remove the build directory.
clean:
    rm -rf {{builddir}}

# Refresh pinned subproject sources after editing subprojects/*.wrap.
bump-wraps:
    meson subprojects update --reset
