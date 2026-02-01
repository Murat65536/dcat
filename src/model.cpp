#include "model.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/GltfMaterial.h>
#include <limits>
#include <cmath>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

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

static glm::vec3 aiVector3DToGlm(const aiVector3D& v) {
    return glm::vec3(v.x, v.y, v.z);
}

static glm::quat aiQuatToGlm(const aiQuaternion& q) {
    return glm::quat(q.w, q.x, q.y, q.z);
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
            vertex.boneIDs = glm::ivec4(-1); // Explicitly initialize to -1

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

// Process node for animated models (no transform baking)
static void processNodeAnimated(aiNode* node, const aiScene* scene,
                               std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                               bool& outHasUVs, Skeleton& skeleton, uint32_t& vertexOffset) {
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

        if (mesh->HasTextureCoords(0)) {
            outHasUVs = true;
        }

        // Process vertices (no transformation applied - keep in bind pose)
        for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
            Vertex vertex{};
            vertex.boneIDs = glm::ivec4(-1); // Explicitly initialize to -1

            vertex.position = glm::vec3(mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z);

            if (mesh->HasTextureCoords(0)) {
                vertex.texcoord = glm::vec2(
                    mesh->mTextureCoords[0][j].x,
                    1.0f - mesh->mTextureCoords[0][j].y
                );
            } else {
                vertex.texcoord = glm::vec2(0.0f, 0.0f);
            }

            if (mesh->HasNormals()) {
                vertex.normal = glm::vec3(mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z);
            } else {
                vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            if (mesh->HasTangentsAndBitangents()) {
                vertex.tangent = glm::vec3(mesh->mTangents[j].x, mesh->mTangents[j].y, mesh->mTangents[j].z);
                vertex.bitangent = glm::vec3(mesh->mBitangents[j].x, mesh->mBitangents[j].y, mesh->mBitangents[j].z);
            } else {
                vertex.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
                vertex.bitangent = glm::vec3(0.0f, 0.0f, 1.0f);
            }

            vertices.push_back(vertex);
        }

        // Extract bone weights
        if (mesh->HasBones()) {
            for (unsigned int j = 0; j < mesh->mNumBones; j++) {
                aiBone* bone = mesh->mBones[j];
                std::string boneName = bone->mName.C_Str();

                int boneIndex = -1;
                auto it = skeleton.boneMap.find(boneName);
                if (it == skeleton.boneMap.end()) {
                    boneIndex = static_cast<int>(skeleton.bones.size());
                    BoneInfo boneInfo;
                    boneInfo.name = boneName;
                    boneInfo.offsetMatrix = aiMatrix4x4ToGlm(bone->mOffsetMatrix);
                    boneInfo.index = boneIndex;
                    skeleton.bones.push_back(boneInfo);
                    skeleton.boneMap[boneName] = boneIndex;
                } else {
                    boneIndex = it->second;
                }

                // Assign bone weights to vertices
                for (unsigned int k = 0; k < bone->mNumWeights; k++) {
                    unsigned int vertexId = bone->mWeights[k].mVertexId + baseIndex;
                    float weight = bone->mWeights[k].mWeight;

                    if (vertexId < vertices.size()) {
                        Vertex& v = vertices[vertexId];
                        // Find empty slot
                        for (int slot = 0; slot < MAX_BONE_INFLUENCE; slot++) {
                            if (v.boneIDs[slot] < 0) {
                                v.boneIDs[slot] = boneIndex;
                                v.boneWeights[slot] = weight;
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Fix for vertices not influenced by any bone (static parts of the mesh)
        // If a vertex has no bone weights, attach it to the node itself
        std::string nodeName = node->mName.C_Str();
        int nodeBoneIndex = -1;

        for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
            uint32_t globalVertexIdx = baseIndex + j;
            if (vertices[globalVertexIdx].boneIDs[0] < 0) {
                // Lazy initialization of the node bone
                if (nodeBoneIndex == -1) {
                    auto it = skeleton.boneMap.find(nodeName);
                    if (it == skeleton.boneMap.end()) {
                        nodeBoneIndex = static_cast<int>(skeleton.bones.size());
                        BoneInfo boneInfo;
                        boneInfo.name = nodeName;
                        boneInfo.offsetMatrix = glm::mat4(1.0f); // Identity because mesh space == node space
                        boneInfo.index = nodeBoneIndex;
                        skeleton.bones.push_back(boneInfo);
                        skeleton.boneMap[nodeName] = nodeBoneIndex;
                    } else {
                        nodeBoneIndex = it->second;
                    }
                }
                
                vertices[globalVertexIdx].boneIDs[0] = nodeBoneIndex;
                vertices[globalVertexIdx].boneWeights[0] = 1.0f;
            }
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
        processNodeAnimated(node->mChildren[i], scene, vertices, indices, outHasUVs, skeleton, vertexOffset);
    }
}

// Build bone hierarchy
static void buildBoneHierarchy(aiNode* node, Skeleton& skeleton, int parentIndex) {
    BoneNode boneNode;
    boneNode.name = node->mName.C_Str();
    boneNode.transformation = aiMatrix4x4ToGlm(node->mTransformation);

    // Decompose transform for fallback
    boneNode.initialPosition = glm::vec3(boneNode.transformation[3]);
    
    // Scale
    boneNode.initialScale.x = glm::length(glm::vec3(boneNode.transformation[0]));
    boneNode.initialScale.y = glm::length(glm::vec3(boneNode.transformation[1]));
    boneNode.initialScale.z = glm::length(glm::vec3(boneNode.transformation[2]));
    
    // Rotation (normalize columns first)
    if (boneNode.initialScale.x > 0.0001f && boneNode.initialScale.y > 0.0001f && boneNode.initialScale.z > 0.0001f) {
        glm::mat3 rotM;
        rotM[0] = glm::vec3(boneNode.transformation[0]) / boneNode.initialScale.x;
        rotM[1] = glm::vec3(boneNode.transformation[1]) / boneNode.initialScale.y;
        rotM[2] = glm::vec3(boneNode.transformation[2]) / boneNode.initialScale.z;
        boneNode.initialRotation = glm::quat_cast(rotM);
    } else {
        boneNode.initialRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    boneNode.parentIndex = parentIndex;

    int currentIndex = static_cast<int>(skeleton.boneHierarchy.size());
    skeleton.boneHierarchy.push_back(boneNode);

    // Update parent's children list
    if (parentIndex >= 0 && parentIndex < static_cast<int>(skeleton.boneHierarchy.size())) {
        skeleton.boneHierarchy[parentIndex].childIndices.push_back(currentIndex);
    }

    // Recursively process children
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        buildBoneHierarchy(node->mChildren[i], skeleton, currentIndex);
    }
}

static void loadAnimations(const aiScene* scene, std::vector<Animation>& animations) {
    for (unsigned int i = 0; i < scene->mNumAnimations; i++) {
        aiAnimation* aiAnim = scene->mAnimations[i];

        Animation animation;
        animation.name = aiAnim->mName.C_Str();
        animation.duration = static_cast<float>(aiAnim->mDuration);
        animation.ticksPerSecond = static_cast<float>(aiAnim->mTicksPerSecond);

        for (unsigned int j = 0; j < aiAnim->mNumChannels; j++) {
            aiNodeAnim* channel = aiAnim->mChannels[j];

            BoneAnimation boneAnim;
            boneAnim.boneName = channel->mNodeName.C_Str();

            // Position keys
            for (unsigned int k = 0; k < channel->mNumPositionKeys; k++) {
                VectorKey key;
                key.time = static_cast<float>(channel->mPositionKeys[k].mTime);
                key.value = aiVector3DToGlm(channel->mPositionKeys[k].mValue);
                boneAnim.positionKeys.push_back(key);
            }

            // Rotation keys
            for (unsigned int k = 0; k < channel->mNumRotationKeys; k++) {
                QuaternionKey key;
                key.time = static_cast<float>(channel->mRotationKeys[k].mTime);
                key.value = aiQuatToGlm(channel->mRotationKeys[k].mValue);
                boneAnim.rotationKeys.push_back(key);
            }

            // Scale keys
            for (unsigned int k = 0; k < channel->mNumScalingKeys; k++) {
                VectorKey key;
                key.time = static_cast<float>(channel->mScalingKeys[k].mTime);
                key.value = aiVector3DToGlm(channel->mScalingKeys[k].mValue);
                boneAnim.scaleKeys.push_back(key);
            }

            animation.boneAnimations.push_back(boneAnim);
        }

        animations.push_back(animation);
    }
}

static std::string resolveTexturePath(const std::string& modelPath, const std::string& texturePath) {
    if (texturePath.empty()) return "";
    
    // Check if absolute path (simple check)
    if (texturePath[0] == '/') return texturePath;
    
    // Get directory from model path
    std::string dir;
    size_t lastSlash = modelPath.rfind('/');
    if (lastSlash != std::string::npos) {
        dir = modelPath.substr(0, lastSlash + 1);
    }
    
    return dir + texturePath;
}

bool loadModel(const std::string& path, Mesh& mesh, bool& outHasUVs, MaterialInfo& outMaterial) {
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

    // Check coordinate system metadata
    int upAxis = 1; // Default Y-up
    int upAxisSign = 1;
    int frontAxis = 2; // Default Z-forward
    int frontAxisSign = 1;
    int coordAxis = 0; // X-right
    int coordAxisSign = 1;
    
    if (scene->mMetaData) {
        scene->mMetaData->Get("UpAxis", upAxis);
        scene->mMetaData->Get("UpAxisSign", upAxisSign);
        scene->mMetaData->Get("FrontAxis", frontAxis);
        scene->mMetaData->Get("FrontAxisSign", frontAxisSign);
        scene->mMetaData->Get("CoordAxis", coordAxis);
        scene->mMetaData->Get("CoordAxisSign", coordAxisSign);
    }

    glm::mat4 coordinateConversion = glm::mat4(1.0f);
    
    if (upAxis == 2 && upAxisSign == 1) {
        coordinateConversion = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    } else if (upAxis == 0 && upAxisSign == 1) {
        coordinateConversion = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    }

    outHasUVs = false;
    mesh.vertices.clear();
    mesh.indices.clear();
    mesh.generation++; // Mark as modified
    mesh.coordinateSystemTransform = coordinateConversion; // Store for later use
    outMaterial = MaterialInfo{};
    mesh.vertices.clear();
    mesh.indices.clear();
    mesh.generation++; // Mark as modified
    outMaterial = MaterialInfo{};

    // Check if model has animations
    mesh.hasAnimations = (scene->mNumAnimations > 0);

    if (mesh.hasAnimations) {
        // Process animated model
        mesh.skeleton = Skeleton{};
        mesh.animations.clear();

        uint32_t vertexOffset = 0;
        processNodeAnimated(scene->mRootNode, scene, mesh.vertices, mesh.indices,
                           outHasUVs, mesh.skeleton, vertexOffset);
        buildBoneHierarchy(scene->mRootNode, mesh.skeleton, -1);
        loadAnimations(scene, mesh.animations);

        mesh.skeleton.globalInverseTransform = glm::inverse(aiMatrix4x4ToGlm(scene->mRootNode->mTransformation));
    } else {
        processNode(scene->mRootNode, scene, glm::mat4(1.0f), mesh.vertices, mesh.indices, outHasUVs);
    }
    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        aiMaterial* material = scene->mMaterials[i];
        aiString str;

        if (outMaterial.diffusePath.empty()) {
            if (material->GetTexture(aiTextureType_DIFFUSE, 0, &str) == AI_SUCCESS) {
                outMaterial.diffusePath = resolveTexturePath(path, str.C_Str());
            } else if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &str) == AI_SUCCESS) { // PBR
                outMaterial.diffusePath = resolveTexturePath(path, str.C_Str());
            }
        }

        // Try to find normal map
        if (outMaterial.normalPath.empty()) {
            if (material->GetTexture(aiTextureType_NORMALS, 0, &str) == AI_SUCCESS) {
                outMaterial.normalPath = resolveTexturePath(path, str.C_Str());
            } else if (material->GetTexture(aiTextureType_HEIGHT, 0, &str) == AI_SUCCESS) { // OBJ often uses HEIGHT for bump/normal
                outMaterial.normalPath = resolveTexturePath(path, str.C_Str());
            } else if (material->GetTexture(aiTextureType_NORMAL_CAMERA, 0, &str) == AI_SUCCESS) {
                outMaterial.normalPath = resolveTexturePath(path, str.C_Str());
            }
        }

        aiString alphaModeStr;
        if (material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaModeStr) == AI_SUCCESS) {
            std::string mode = alphaModeStr.C_Str();
            if (mode == "MASK") {
                outMaterial.alphaMode = AlphaMode::Mask;
            } else if (mode == "BLEND") {
                outMaterial.alphaMode = AlphaMode::Blend;
            } else {
                outMaterial.alphaMode = AlphaMode::Opaque;
            }
        }
        if (!outMaterial.diffusePath.empty() && !outMaterial.normalPath.empty()) {
            break;
        }
    }
    
    return !mesh.vertices.empty();
}

