#version 450

layout(set = 0, binding = 1) uniform sampler2D diffuseTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;

layout(set = 0, binding = 3) uniform FragmentUniforms {
    vec3 lightDir;
    uint enableLighting;
    vec3 cameraPos;
    float fogStart;
    vec3 fogColor;
    float fogEnd;
    uint useTriplanarMapping;
} fragUniforms;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragWorldNormal;
layout(location = 2) in vec3 fragWorldTangent;
layout(location = 3) in vec3 fragWorldBitangent;
layout(location = 4) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

vec3 getTriplanarColor(vec3 worldPos, vec3 normal) {
    vec3 blendWeights = abs(normal);
    // Tighten the blending to reduce blurring
    blendWeights = pow(blendWeights, vec3(4.0));
    blendWeights = blendWeights / (blendWeights.x + blendWeights.y + blendWeights.z);
    
    vec3 colorX = texture(diffuseTexture, worldPos.zy).rgb;
    vec3 colorY = texture(diffuseTexture, worldPos.xz).rgb;
    vec3 colorZ = texture(diffuseTexture, worldPos.xy).rgb;
    
    return colorX * blendWeights.x + colorY * blendWeights.y + colorZ * blendWeights.z;
}

void main() {
    vec4 diffuseColor;
    
    if (fragUniforms.useTriplanarMapping != 0u) {
        vec3 triplanar = getTriplanarColor(fragWorldPos, normalize(fragWorldNormal));
        diffuseColor = vec4(triplanar, 1.0);
    } else {
        diffuseColor = texture(diffuseTexture, fragTexCoord);
    }
    
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

    // Calculate fog
    float dist = distance(fragWorldPos, fragUniforms.cameraPos);
    float fogFactor = clamp((dist - fragUniforms.fogStart) / (fragUniforms.fogEnd - fragUniforms.fogStart), 0.0, 1.0);
    finalColor = mix(finalColor, fragUniforms.fogColor, fogFactor);

    outColor = vec4(finalColor, 1.0);
}
