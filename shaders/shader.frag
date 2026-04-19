#version 450

layout(set = 0, binding = 1) uniform sampler2D diffuseTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;

layout(set = 0, binding = 3) uniform FragmentUniforms {
    vec3 lightDir;
    uint enableLighting;
    vec3 cameraPos;
    uint useTriplanarMapping;
    uint alphaMode;    // 0: OPAQUE, 1: MASK, 2: BLEND
    float alphaCutoff;
    float specularStrength;
    float shininess;
    uint useDiffuseAlphaAsLuster;
    uint _pad0;
    uint _pad1;
    uint _pad2;
    vec4 baseColor;
    vec4 hemisphereSkyColor;
    vec4 hemisphereGroundColor;
    vec4 fillLightDir;    // xyz = direction, w = intensity
    vec4 rimLightDir;     // xyz = direction, w = intensity
} fragUniforms;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragWorldNormal;
layout(location = 2) in vec3 fragWorldTangent;
layout(location = 3) in vec3 fragWorldBitangent;
layout(location = 4) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

vec3 applyToneMappingAndGamma(vec3 hdrColor) {
    const float exposure = 1.0;
    const float gamma = 2.2;

    // Simple HDR-style exposure tone mapping.
    vec3 mapped = vec3(1.0) - exp(-max(hdrColor, vec3(0.0)) * exposure);
    // Final output gamma encoding for display.
    return pow(mapped, vec3(1.0 / gamma));
}

vec4 getTriplanarColor(vec3 worldPos, vec3 normal) {
    vec3 blendWeights = abs(normal);
    // Tighten the blending to reduce blurring
    blendWeights = pow(blendWeights, vec3(4.0));
    blendWeights = blendWeights / (blendWeights.x + blendWeights.y + blendWeights.z);
    
    vec4 colorX = texture(diffuseTexture, worldPos.zy);
    vec4 colorY = texture(diffuseTexture, worldPos.xz);
    vec4 colorZ = texture(diffuseTexture, worldPos.xy);
    
    return colorX * blendWeights.x + colorY * blendWeights.y + colorZ * blendWeights.z;
}

void main() {
    vec4 diffuseColor;
    
    if (fragUniforms.useTriplanarMapping != 0u) {
        diffuseColor = getTriplanarColor(fragWorldPos, normalize(fragWorldNormal));
    } else {
        diffuseColor = texture(diffuseTexture, fragTexCoord);
    }
    
    diffuseColor *= fragUniforms.baseColor;

    float sampledAlpha = diffuseColor.a;

    // Alpha handling
    if (fragUniforms.alphaMode == 0u) { // OPAQUE
        diffuseColor.a = 1.0;
    } else if (fragUniforms.alphaMode == 1u) { // MASK
        if (diffuseColor.a < fragUniforms.alphaCutoff) {
            discard;
        }
        diffuseColor.a = 1.0; // Usually mask implies opaque surface where visible
    }
    // BLEND (2) - keep original alpha, no discard

    if (fragUniforms.enableLighting == 0u) {
        outColor = vec4(applyToneMappingAndGamma(diffuseColor.rgb), diffuseColor.a);
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
    
    // Key light (directional)
    vec3 lightDir = normalize(fragUniforms.lightDir);
    vec3 viewDir = normalize(fragUniforms.cameraPos - fragWorldPos);
    float keyDiffuse = max(dot(perturbedNormal, lightDir), 0.0);

    // Hemisphere ambient: interpolate between ground and sky based on up-facing
    float hemisphereBlend = dot(perturbedNormal, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5;
    vec3 ambientColor = mix(fragUniforms.hemisphereGroundColor.rgb,
                            fragUniforms.hemisphereSkyColor.rgb,
                            hemisphereBlend);

    // Fill light (diffuse only, no specular)
    vec3 fillDir = normalize(fragUniforms.fillLightDir.xyz);
    float fillDiffuse = max(dot(perturbedNormal, fillDir), 0.0) * fragUniforms.fillLightDir.w;

    // Rim light (edge-based, view-dependent)
    vec3 rimDir = normalize(fragUniforms.rimLightDir.xyz);
    float rimDot = 1.0 - max(dot(perturbedNormal, viewDir), 0.0);
    float rimFacing = max(dot(perturbedNormal, rimDir), 0.0);
    float rimContrib = pow(rimDot, 3.0) * rimFacing * fragUniforms.rimLightDir.w;

    // Specular (key light only)
    float specularStrength = clamp(fragUniforms.specularStrength, 0.0, 1.0);
    float specularShininess = clamp(fragUniforms.shininess, 8.0, 256.0);
    if (fragUniforms.useDiffuseAlphaAsLuster != 0u) {
        specularStrength = max(specularStrength, sampledAlpha);
        specularShininess = max(specularShininess, mix(12.0, 160.0, sampledAlpha));
    }

    float specularIntensity = 0.0;
    if (keyDiffuse > 0.0 && specularStrength > 0.0) {
        vec3 halfVector = normalize(lightDir + viewDir);
        float specAngle = max(dot(perturbedNormal, halfVector), 0.0);
        float fresnel = pow(1.0 - max(dot(perturbedNormal, viewDir), 0.0), 5.0);
        specularIntensity = pow(specAngle, specularShininess) * specularStrength * (0.08 + 0.92 * fresnel);
    }

    vec3 finalColor = diffuseColor.rgb * (keyDiffuse + fillDiffuse)
                    + diffuseColor.rgb * ambientColor
                    + vec3(specularIntensity + rimContrib);

    outColor = vec4(applyToneMappingAndGamma(finalColor), diffuseColor.a);
}
