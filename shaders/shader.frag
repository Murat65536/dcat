#version 450

layout(set = 0, binding = 1) uniform sampler2D diffuseTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;

layout(set = 0, binding = 3) uniform FragmentUniforms {
    vec3 lightDir;
    uint enableLighting;
} fragUniforms;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragWorldNormal;
layout(location = 2) in vec3 fragWorldTangent;
layout(location = 3) in vec3 fragWorldBitangent;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 diffuseColor = texture(diffuseTexture, fragTexCoord);
    
    if (fragUniforms.enableLighting == 0u) {
        outColor = vec4(diffuseColor.rgb, 1.0);
        return;
    }
    
    vec3 normalMapSample = texture(normalTexture, fragTexCoord).rgb;
    vec3 tangentNormal = normalize(normalMapSample * 2.0 - vec3(1.0));
    
    vec3 N = normalize(fragWorldNormal);
    vec3 T = normalize(fragWorldTangent);
    vec3 B = normalize(fragWorldBitangent);
    
    vec3 perturbedNormal = normalize(
        tangentNormal.x * T +
        tangentNormal.y * B +
        tangentNormal.z * N
    );
    
    // Calculate both diffuse and ambient lighting
    float diffuseIntensity = max(dot(perturbedNormal, fragUniforms.lightDir), 0.0);
    float ambientIntensity = 0.5;
    float totalIntensity = diffuseIntensity + ambientIntensity;

    // Clamp to prevent over-brightening
    totalIntensity = min(totalIntensity, 1.0);

    vec3 finalColor = diffuseColor.rgb * totalIntensity;

    outColor = vec4(finalColor, 1.0);
}
