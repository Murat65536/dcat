#!/bin/bash
echo "Creating dist folder..."
mkdir -p dist

echo "Copying dcat.exe..."
cp buildDir/dcat.exe dist/

echo "Collecting required MSYS2 DLLs..."
# Use ldd to find dependencies, filter out Windows system DLLs, and copy them to dist
ldd buildDir/dcat.exe | grep -iv '/c/Windows' | awk '{print $3}' | while read dll; do 
    if [ -f "$dll" ]; then
        echo "Copying $dll..."
        cp "$dll" dist/
    fi
done

echo "Done! The dist folder is ready to be packaged."