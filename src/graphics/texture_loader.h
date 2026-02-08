#ifndef DCAT_TEXTURE_LOADER_H
#define DCAT_TEXTURE_LOADER_H

#include "texture.h"
#include "model.h"

// Load diffuse texture from file, embedded data, or use default
bool load_diffuse_texture(const char* model_path, const char* texture_arg,
                          const MaterialInfo* material_info, Texture* out_texture);

// Load normal map texture or create flat default
bool load_normal_texture(const char* normal_arg, const MaterialInfo* material_info,
                         Texture* out_texture);

// Load skydome texture and mesh
bool load_skydome(const char* skydome_path, Mesh* skydome_mesh, 
                  Texture* skydome_texture);

#endif
