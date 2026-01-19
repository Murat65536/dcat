#include "model.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <limits>
#include <cmath>
#include <iostream>

CameraSetup calculateCameraSetup(const std::vector<Vertex>& vertices) {
    if (vertices.empty()) {
        return {
            glm::vec3(0.0f, 0.0f, 3.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            1.0f
        };
    }

    glm::vec3 minPos(std::numeric_limits<float>::infinity());
    glm::vec3 maxPos(-std::numeric_limits<float>::infinity());

    for (const auto& vertex : vertices) {
        minPos = glm::min(minPos, vertex.position);
        maxPos = glm::max(maxPos, vertex.position);
    }

    glm::vec3 center = (minPos + maxPos) * 0.5f;
    glm::vec3 size = maxPos - minPos;
    float diagonal = std::sqrt(size.x * size.x + size.y * size.y + size.z * size.z);
    float distance = diagonal * 1.2f;

    glm::vec3 cameraOffset(
        diagonal * 0.3f,
        diagonal * 0.2f,
        distance
    );

    glm::vec3 position = center + cameraOffset;

    return { position, center, diagonal };
}

bool loadModel(const std::string& path, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    Assimp::Importer importer;
    
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_CalcTangentSpace
    );

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "Error loading model: " << importer.GetErrorString() << std::endl;
        return false;
    }

    if (scene->mNumMeshes == 0) {
        std::cerr << "No meshes found in model file" << std::endl;
        return false;
    }

    const aiMesh* mesh = scene->mMeshes[0];
    
    vertices.reserve(mesh->mNumVertices);
    
    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        Vertex vertex{};
        
        vertex.position = glm::vec3(
            mesh->mVertices[i].x,
            mesh->mVertices[i].y,
            mesh->mVertices[i].z
        );
        
        if (mesh->HasTextureCoords(0)) {
            vertex.texcoord = glm::vec2(
                mesh->mTextureCoords[0][i].x,
                1.0f - mesh->mTextureCoords[0][i].y
            );
        } else {
            vertex.texcoord = glm::vec2(0.0f, 0.0f);
        }
        
        if (mesh->HasNormals()) {
            vertex.normal = glm::vec3(
                mesh->mNormals[i].x,
                mesh->mNormals[i].y,
                mesh->mNormals[i].z
            );
        } else {
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        
        if (mesh->HasTangentsAndBitangents()) {
            vertex.tangent = glm::vec3(
                mesh->mTangents[i].x,
                mesh->mTangents[i].y,
                mesh->mTangents[i].z
            );
            vertex.bitangent = glm::vec3(
                mesh->mBitangents[i].x,
                mesh->mBitangents[i].y,
                mesh->mBitangents[i].z
            );
        } else {
            vertex.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            vertex.bitangent = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        
        vertices.push_back(vertex);
    }
    
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        const aiFace& face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j]);
        }
    }
    
    return true;
}
