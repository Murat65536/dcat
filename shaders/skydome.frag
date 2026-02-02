#version 450

layout(set = 0, binding = 0) uniform sampler2D skyTexture;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 skyColor = texture(skyTexture, fragTexCoord);
    outColor = vec4(skyColor.rgb, 1.0);
}
