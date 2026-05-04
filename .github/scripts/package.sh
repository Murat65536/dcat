#!/bin/bash

set -e

: "${BUILD_DIR:=buildDir}"

# 1. Create directories
rm -rf dist
mkdir -p dist/shaders

# 2. Copy the executable, shaders, and docs
cp "$BUILD_DIR/dcat.exe" dist/
cp "$BUILD_DIR"/shaders/*.spv dist/shaders/ 2>/dev/null || true
cp LICENSE README.md dist/ 2>/dev/null || true

resolve_dll_path() {
    local dll_name="$1"
    local dll_path
    dll_path="$(command -v "$dll_name" 2>/dev/null || true)"
    if [ -n "$dll_path" ] && [ -f "$dll_path" ]; then
        echo "$dll_path"
        return 0
    fi

    local path_dir
    IFS=':' read -r -a path_dirs <<< "$PATH"
    for path_dir in "${path_dirs[@]}"; do
        if [ -f "$path_dir/$dll_name" ]; then
            echo "$path_dir/$dll_name"
            return 0
        fi
    done
    return 1
}

is_windows_system_path() {
    local path_lc
    path_lc="$(echo "$1" | tr '[:upper:]' '[:lower:]' | tr '\\' '/')"
    [[ "$path_lc" == /c/windows/* || "$path_lc" == /mnt/c/windows/* || "$path_lc" == c:/windows/* ]]
}

collect_non_system_dll_paths() {
    local binary="$1"
    local ldd_deps

    ldd_deps="$(ldd "$binary" 2>/dev/null | grep '=>' | awk '{print $3}' || true)"
    if [ -n "$ldd_deps" ]; then
        printf '%s\n' "$ldd_deps" | while read -r dll_path; do
            if ! is_windows_system_path "$dll_path"; then
                printf '%s\n' "$dll_path"
            fi
        done
        return 0
    fi

    objdump -p "$binary" 2>/dev/null | awk '/DLL Name:/ {print $3}' | while read -r dll_name; do
        case "${dll_name^^}" in
            KERNEL32.DLL|USER32.DLL|GDI32.DLL|ADVAPI32.DLL|SHELL32.DLL|OLE32.DLL|OLEAUT32.DLL|COMDLG32.DLL|WS2_32.DLL|WINMM.DLL|VERSION.DLL|IMM32.DLL|SETUPAPI.DLL|SHLWAPI.DLL|COMCTL32.DLL|UCRTBASE.DLL|VCRUNTIME*.DLL|API-MS-WIN-CRT-*.DLL)
                continue
                ;;
        esac
        dll_path="$(resolve_dll_path "$dll_name" || true)"
        if [ -n "$dll_path" ] && ! is_windows_system_path "$dll_path"; then
            printf '%s\n' "$dll_path"
        fi
    done | sort -u
}

# 3. Find and copy all non-system DLLs
echo "Collecting DLLs..."
export PATH="$(pwd)/dist:$PATH"
touch dist/.new_dll
while [ -f dist/.new_dll ]; do
    rm dist/.new_dll
    for f in dist/*.exe dist/*.dll; do
        if [ ! -f "$f" ]; then continue; fi
        collect_non_system_dll_paths "$f" | while read -r dll_path; do
            if is_windows_system_path "$dll_path"; then
                continue
            fi
            if [ -f "$dll_path" ] && [ ! -f "dist/$(basename "$dll_path")" ]; then
                echo "Copying $(basename "$dll_path")..."
                cp "$dll_path" dist/
                touch dist/.new_dll
            fi
        done
    done
done

echo "Smoke-testing packaged executable..."
PATH="$(pwd)/dist:$PATH" dist/dcat.exe --help >/dev/null

echo "Done! All DLLs copied to dist folder."
