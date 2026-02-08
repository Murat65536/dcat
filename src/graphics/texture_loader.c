#include "texture_loader.h"
#include "skydome.h"
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <stdlib.h>
#include <stdio.h>

bool load_diffuse_texture(const char* model_path, const char* texture_arg,
                          const MaterialInfo* material_info, Texture* out_texture) {
    const char* final_path = texture_arg ? texture_arg : material_info->diffuse_path;
    
    if (!final_path || final_path[0] == '\0') {
        texture_init_default(out_texture);
        return true;
    }
    
    if (final_path[0] == '*') {
        const struct aiScene* scene = aiImportFile(model_path, 0);
        if (!scene || scene->mNumTextures == 0) {
            texture_init_default(out_texture);
            aiReleaseImport(scene);
            return true;
        }
        
        int tex_index = atoi(final_path + 1);
        if (tex_index < 0 || tex_index >= (int)scene->mNumTextures) {
            texture_init_default(out_texture);
            aiReleaseImport(scene);
            return true;
        }
        
        const struct aiTexture* embedded_tex = scene->mTextures[tex_index];
        if (embedded_tex->mHeight == 0) {
            texture_from_memory(out_texture, 
                              (const unsigned char*)embedded_tex->pcData,
                              embedded_tex->mWidth);
        } else {
            out_texture->width = embedded_tex->mWidth;
            out_texture->height = embedded_tex->mHeight;
            out_texture->data_size = embedded_tex->mWidth * embedded_tex->mHeight * 4;
            out_texture->data = malloc(out_texture->data_size);
            
            for (unsigned int i = 0; i < embedded_tex->mWidth * embedded_tex->mHeight; i++) {
                out_texture->data[i * 4 + 0] = embedded_tex->pcData[i].r;
                out_texture->data[i * 4 + 1] = embedded_tex->pcData[i].g;
                out_texture->data[i * 4 + 2] = embedded_tex->pcData[i].b;
                out_texture->data[i * 4 + 3] = embedded_tex->pcData[i].a;
            }
        }
        
        aiReleaseImport(scene);
        return true;
    }
    
    return texture_from_file(out_texture, final_path);
}

bool load_normal_texture(const char* normal_arg, const MaterialInfo* material_info,
                         Texture* out_texture) {
    const char* final_path = normal_arg ? normal_arg : material_info->normal_path;
    
    if (final_path && final_path[0] != '\0') {
        return texture_from_file(out_texture, final_path);
    }
    
    texture_create_flat_normal_map(out_texture);
    return true;
}

bool load_skydome(const char* skydome_path, Mesh* skydome_mesh,
                  Texture* skydome_texture) {
    if (!skydome_path) {
        return false;
    }
    
    generate_skydome(skydome_mesh, 100.0f, 32, 16);
    
    if (!texture_from_file(skydome_texture, skydome_path)) {
        fprintf(stderr, "Warning: Failed to load skydome texture\n");
        mesh_free(skydome_mesh);
        return false;
    }
    
    return true;
}
