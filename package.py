import os
import re
import shutil
import subprocess
import sys

def get_imported_dlls(filepath):
    try:
        data = open(filepath, 'rb').read()
        dlls = set(re.findall(rb'[A-Za-z0-9_-]+\.dll', data, re.IGNORECASE))
        return [d.decode().lower() for d in dlls]
    except Exception:
        return []

def main():
    msys2_bins = []
    
    if sys.executable:
        msys2_bins.append(os.path.dirname(sys.executable))
        
    if "MSYSTEM_PREFIX" in os.environ:
        msys2_bins.append(os.path.join(os.environ["MSYSTEM_PREFIX"], "bin"))
        
    msys2_bins.extend([
        r"C:\msys64\mingw64\bin",
        r"C:\msys64\ucrt64\bin",
        r"C:\msys64\clang64\bin",
        r"C:\msys64\usr\bin"
    ])

    dist_dir = "dist"
    os.makedirs(dist_dir, exist_ok=True)
    exe_path = os.path.join("buildDir", "dcat.exe")
    shutil.copy(exe_path, dist_dir)
    
    known_system_dlls = {"kernel32.dll", "user32.dll", "vulkan-1.dll", "advapi32.dll", "gdi32.dll", "shell32.dll", "ole32.dll", "ws2_32.dll", "msvcrt.dll"}
    
    queue = [os.path.join(dist_dir, "dcat.exe")]
    processed = set()
    copied = set()
    
    print("Collecting DLLs...")
    while queue:
        current_file = queue.pop(0)
        if current_file in processed:
            continue
        processed.add(current_file)
        
        imported = get_imported_dlls(current_file)
        for dll in imported:
            if dll in known_system_dlls or dll.startswith("api-ms-win"):
                continue
            
            src_path = None
            for bin_path in msys2_bins:
                candidate = os.path.join(bin_path, dll)
                if os.path.exists(candidate):
                    src_path = candidate
                    break
            
            if src_path and dll not in copied:
                dst_path = os.path.join(dist_dir, dll)
                print(f"Copying {dll}...")
                shutil.copy(src_path, dst_path)
                copied.add(dll)
                queue.append(dst_path)

    print("Done! All DLLs copied to dist folder.")

if __name__ == "__main__":
    main()
