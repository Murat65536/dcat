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

# Drop into an environment with the built binary on PATH.
devenv:
    meson devenv -C {{builddir}}

# Remove the build directory.
clean:
    rm -rf {{builddir}}

# Refresh pinned subproject sources after editing subprojects/*.wrap.
bump-wraps:
    meson subprojects update --reset
