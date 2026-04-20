#include "vk_shader.h"
#include "core/platform_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

static void normalize_path_separators(char *path) {
#ifdef _WIN32
    for (char *p = path; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
#else
    (void)path;
#endif
}

static void get_executable_directory(char *out, const size_t out_size) {
    if (out_size == 0) {
        return;
    }
    out[0] = '\0';

#ifdef _WIN32
    const DWORD len = GetModuleFileNameA(NULL, out, out_size);
    if (len == 0 || len >= out_size) {
        out[0] = '\0';
        return;
    }
    normalize_path_separators(out);
#else
    ssize_t len = readlink("/proc/self/exe", out, out_size - 1);
    if (len == -1) {
        out[0] = '\0';
        return;
    }
    out[len] = '\0';
#endif

    char *last_slash = strrchr(out, '/');
    if (!last_slash) {
        out[0] = '\0';
        return;
    }
    last_slash[1] = '\0';
}

char* read_shader_file(VulkanRenderer* r, const char* filename, size_t* out_size) {
    enum { MAX_SHADER_FILENAME_LEN = 255, MAX_SHADER_DIR_LEN = 240 };
#ifdef _WIN32
    const char* search_paths[] = {
        "",  // Will be replaced with exe_dir/shaders/
        "",  // Will be replaced with exe_dir
        "./shaders/",
        "../share/dcat/shaders/",
    };
#else
    const char* search_paths[] = {
        "",  // Will be replaced with exe_dir/shaders/
        "",  // Will be replaced with exe_dir (for build directory)
        "./shaders/",
        "/usr/local/share/dcat/shaders/",
        "/usr/share/dcat/shaders/"
    };
#endif

    // Get executable directory
    char exe_dir[4096] = {0};
    get_executable_directory(exe_dir, sizeof(exe_dir));
    
    // Try shader directory first if set
    if (r->shader_directory[0]) {
        char path[512];
        snprintf(path, sizeof(path), "%s%.*s", r->shader_directory, MAX_SHADER_FILENAME_LEN, filename);
        FILE* f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            const long file_size = ftell(f);
            fseek(f, 0, SEEK_SET);

            if (file_size < 0) {
                fclose(f);
                return NULL;
            }

            *out_size = (size_t)file_size;
            char* buffer = malloc(*out_size);
            if (!buffer) {
                fclose(f);
                return NULL;
            }

            if (fread(buffer, 1, *out_size, f) != *out_size) {
                free(buffer);
                fclose(f);
                return NULL;
            }

            fclose(f);
            return buffer;
        }
    }
    
    // Try various paths
    for (size_t i = 0; i < (sizeof(search_paths) / sizeof(search_paths[0])); i++) {
        char path[512];
        if (i == 0 && exe_dir[0]) {
            snprintf(path, sizeof(path), "%.*sshaders/%.*s", MAX_SHADER_DIR_LEN, exe_dir, MAX_SHADER_FILENAME_LEN, filename);
        } else if (i == 1 && exe_dir[0]) {
            snprintf(path, sizeof(path), "%.*s%.*s", MAX_SHADER_DIR_LEN, exe_dir, MAX_SHADER_FILENAME_LEN, filename);
        } else {
            snprintf(path, sizeof(path), "%s%.*s", search_paths[i], MAX_SHADER_FILENAME_LEN, filename);
        }
        normalize_path_separators(path);
        
        FILE* f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            const long file_size = ftell(f);
            fseek(f, 0, SEEK_SET);

            if (file_size < 0) {
                fclose(f);
                continue;
            }

            *out_size = (size_t)file_size;
            char* buffer = malloc(*out_size);
            if (!buffer) {
                fclose(f);
                continue;
            }

            if (fread(buffer, 1, *out_size, f) != *out_size) {
                free(buffer);
                fclose(f);
                continue;
            }

            fclose(f);
            
            // Cache shader directory
            const char* last_slash = strrchr(path, '/');
            if (last_slash) {
                const size_t dir_len = last_slash - path + 1;
                if (dir_len < sizeof(r->shader_directory)) {
                    memcpy(r->shader_directory, path, dir_len);
                    r->shader_directory[dir_len] = '\0';
                }
            }

            return buffer;
        }
    }
    
    fprintf(stderr, "Failed to find shader file: %s\n", filename);
    return NULL;
}

VkShaderModule create_shader_module(VulkanRenderer* r, const char* code, size_t size) {
    VkShaderModuleCreateInfo create_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    create_info.codeSize = size;
    create_info.pCode = (const uint32_t*)code;
    
    VkShaderModule shader_module;
    if (vkCreateShaderModule(r->device, &create_info, NULL, &shader_module) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create shader module\n");
        return VK_NULL_HANDLE;
    }
    return shader_module;
}
