#!/bin/bash

set -e

# 1. Create directories
mkdir -p dist/shaders

# 2. Copy the executable, shaders, and docs
cp buildDir/dcat.exe dist/
cp buildDir/shaders/*.spv dist/shaders/ 2>/dev/null || true
cp LICENSE README.md dist/ 2>/dev/null || true

# 3. Find and copy all non-system DLLs
echo "Collecting DLLs..."
touch dist/.new_dll
while [ -f dist/.new_dll ]; do
    rm dist/.new_dll
    for f in dist/*.exe dist/*.dll; do
        if [ ! -f "$f" ]; then continue; fi
        ldd "$f" 2>/dev/null | grep '=>' | grep -v -i '/c/windows/' | awk '{print $3}' | while read -r dll_path; do
            if [ -f "$dll_path" ] && [ ! -f "dist/$(basename "$dll_path")" ]; then
                echo "Copying $(basename "$dll_path")..."
                cp "$dll_path" dist/
                touch dist/.new_dll
            fi
        done
    done
done

echo "Done! All DLLs copied to dist folder."
