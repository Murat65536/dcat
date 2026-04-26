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
ldd buildDir/dcat.exe | grep '=>' | grep -v -i '/c/windows/' | awk '{print $3}' | while read -r dll_path; do
    # Some paths might be empty if the library isn't found, but ldd usually reports "not found"
    if [ -f "$dll_path" ]; then
        echo "Copying $(basename "$dll_path")..."
        cp "$dll_path" dist/
    fi
done

echo "Done! All DLLs copied to dist folder."
