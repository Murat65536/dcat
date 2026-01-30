#include "model.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <limits>
#include <cmath>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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

static glm::mat4 aiMatrix4x4ToGlm(const aiMatrix4x4& from) {
    glm::mat4 to;
    to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
    to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
    to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
    to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
    return to;
}

static void processNode(aiNode* node, const aiScene* scene, const glm::mat4& parentTransform,
                        std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, bool& outHasUVs) {
    glm::mat4 nodeTransform = parentTransform * aiMatrix4x4ToGlm(node->mTransformation);

    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

        if (mesh->HasTextureCoords(0)) {
            outHasUVs = true;
        }

        // Process vertices
        for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
            Vertex vertex{};
            
            // Apply transformation
            glm::vec4 pos = nodeTransform * glm::vec4(mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z, 1.0f);
            vertex.position = glm::vec3(pos);
            
            if (mesh->HasTextureCoords(0)) {
                vertex.texcoord = glm::vec2(
                    mesh->mTextureCoords[0][j].x,
                    1.0f - mesh->mTextureCoords[0][j].y
                );
            } else {
                vertex.texcoord = glm::vec2(0.0f, 0.0f);
            }
            
            // Transform normal, tangent, bitangent (using normal matrix)
            glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(nodeTransform)));
            
            if (mesh->HasNormals()) {
                vertex.normal = glm::normalize(normalMatrix * glm::vec3(mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z));
            } else {
                vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            
            if (mesh->HasTangentsAndBitangents()) {
                vertex.tangent = glm::normalize(normalMatrix * glm::vec3(mesh->mTangents[j].x, mesh->mTangents[j].y, mesh->mTangents[j].z));
                vertex.bitangent = glm::normalize(normalMatrix * glm::vec3(mesh->mBitangents[j].x, mesh->mBitangents[j].y, mesh->mBitangents[j].z));
            } else {
                vertex.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
                vertex.bitangent = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            
            vertices.push_back(vertex);
        }

        // Process indices
        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            aiFace face = mesh->mFaces[j];
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                indices.push_back(baseIndex + face.mIndices[k]);
            }
        }
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(node->mChildren[i], scene, nodeTransform, vertices, indices, outHasUVs);
    }
}

bool loadModel(const std::string& path, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, bool& outHasUVs) {
    Assimp::Importer importer;
    
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_SortByPType
    );

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "Error loading model: " << importer.GetErrorString() << std::endl;
        return false;
    }

    outHasUVs = false;
    vertices.clear();
    indices.clear();

    processNode(scene->mRootNode, scene, glm::mat4(1.0f), vertices, indices, outHasUVs);
    
    return !vertices.empty();
}

