#version 450

layout(set = 0, binding = 0) uniform Uniforms {
    mat4 mvp;
    mat4 model;
} uniforms;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragWorldNormal;
layout(location = 2) out vec3 fragWorldTangent;
layout(location = 3) out vec3 fragWorldBitangent;

void main() {
    gl_Position = uniforms.mvp * vec4(inPosition, 1.0);
    fragTexCoord = inTexCoord;
    fragWorldNormal = (uniforms.model * vec4(inNormal, 0.0)).xyz;
    fragWorldTangent = (uniforms.model * vec4(inTangent, 0.0)).xyz;
    fragWorldBitangent = (uniforms.model * vec4(inBitangent, 0.0)).xyz;
}
