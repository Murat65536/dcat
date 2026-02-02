#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
} pushConstants;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;

void main() {
    vec4 pos = pushConstants.mvp * vec4(inPosition, 1.0);
    // Force depth to far plane so skydome renders behind everything
    gl_Position = pos.xyww;
    fragTexCoord = inTexCoord;
}
