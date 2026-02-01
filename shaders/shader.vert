#version 450

// Push constants for frequently updated per-draw data
layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
} pushConstants;

// Uniform buffer for bone animation data (less frequently updated)
layout(set = 0, binding = 0) uniform Uniforms {
    mat4 boneMatrices[200];
    uint hasAnimation;
} uniforms;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in ivec4 inBoneIDs;
layout(location = 6) in vec4 inBoneWeights;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragWorldNormal;
layout(location = 2) out vec3 fragWorldTangent;
layout(location = 3) out vec3 fragWorldBitangent;
layout(location = 4) out vec3 fragWorldPos;

void main() {
    vec4 localPosition;
    vec3 localNormal;
    vec3 localTangent;
    vec3 localBitangent;

    if (uniforms.hasAnimation == 1u) {
        // GPU skinning
        mat4 boneTransform = mat4(0.0);
        for (int i = 0; i < 4; i++) {
            if (inBoneIDs[i] >= 0) {
                boneTransform += uniforms.boneMatrices[inBoneIDs[i]] * inBoneWeights[i];
            }
        }

        localPosition = boneTransform * vec4(inPosition, 1.0);
        localNormal = mat3(boneTransform) * inNormal;
        localTangent = mat3(boneTransform) * inTangent;
        localBitangent = mat3(boneTransform) * inBitangent;
    } else {
        // Static model
        localPosition = vec4(inPosition, 1.0);
        localNormal = inNormal;
        localTangent = inTangent;
        localBitangent = inBitangent;
    }

    gl_Position = pushConstants.mvp * localPosition;
    fragTexCoord = inTexCoord;
    fragWorldNormal = (pushConstants.model * vec4(localNormal, 0.0)).xyz;
    fragWorldTangent = (pushConstants.model * vec4(localTangent, 0.0)).xyz;
    fragWorldBitangent = (pushConstants.model * vec4(localBitangent, 0.0)).xyz;
    fragWorldPos = (pushConstants.model * localPosition).xyz;
}
