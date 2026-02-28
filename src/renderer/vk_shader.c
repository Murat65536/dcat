#include "vk_shader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char* read_shader_file(VulkanRenderer* r, const char* filename, size_t* out_size) {
    enum { MAX_SHADER_FILENAME_LEN = 255 };
    enum { MAX_SHADER_DIR_LEN = 240 };
    const char* search_paths[] = {
        "",  // Will be replaced with shader_directory if set
        "./shaders/",
        "/usr/local/share/dcat/shaders/",
        "/usr/share/dcat/shaders/"
    };
    
    // Get executable directory
    char exe_path[4096] = {0};
    char exe_dir[4096] = {0};
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        char* last_slash = strrchr(exe_path, '/');
        if (last_slash) {
            size_t dir_len = last_slash - exe_path + 1;
            memcpy(exe_dir, exe_path, dir_len);
            exe_dir[dir_len] = '\0';
        }
    }
    
    // Try shader directory first if set
    if (r->shader_directory[0]) {
        char path[512];
        snprintf(path, sizeof(path), "%s%.*s", r->shader_directory, MAX_SHADER_FILENAME_LEN, filename);
        FILE* f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            *out_size = ftell(f);
            fseek(f, 0, SEEK_SET);
            char* buffer = malloc(*out_size);
            fread(buffer, 1, *out_size, f);
            fclose(f);
            return buffer;
        }
    }
    
    // Try various paths
    for (size_t i = 0; i < (sizeof(search_paths) / sizeof(search_paths[0])); i++) {
        char path[512];
        if (i == 0 && exe_dir[0]) {
            snprintf(path, sizeof(path), "%.*sshaders/%.*s", MAX_SHADER_DIR_LEN, exe_dir, MAX_SHADER_FILENAME_LEN, filename);
        } else {
            snprintf(path, sizeof(path), "%s%.*s", search_paths[i], MAX_SHADER_FILENAME_LEN, filename);
        }
        
        FILE* f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            *out_size = ftell(f);
            fseek(f, 0, SEEK_SET);
            char* buffer = malloc(*out_size);
            fread(buffer, 1, *out_size, f);
            fclose(f);
            
            // Cache shader directory
            char* last_slash = strrchr(path, '/');
            if (last_slash) {
                size_t dir_len = last_slash - path + 1;
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
