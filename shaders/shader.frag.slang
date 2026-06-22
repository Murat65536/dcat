import common;

[[vk::binding(1, 0)]] Sampler2D diffuseTexture;
[[vk::binding(2, 0)]] Sampler2D normalTexture;

struct FragmentUniforms {
    float3 lightDir;
    uint enableLighting;
    float3 cameraPos;
    uint useTriplanarMapping;
    uint alphaMode;    // 0: OPAQUE, 1: MASK, 2: BLEND
    float alphaCutoff;
    float specularStrength;
    float shininess;
    uint useDiffuseAlphaAsLuster;
    uint _pad0;
    uint _pad1;
    uint _pad2;
    float4 baseColor;
    float4 hemisphereSkyColor;
    float4 hemisphereGroundColor;
    float4 fillLightDir;    // xyz = direction, w = intensity
    float4 rimLightDir;     // xyz = direction, w = intensity
};
[[vk::binding(3, 0)]] ConstantBuffer<FragmentUniforms> fragUniforms;

struct FSInput {
    [[vk::location(0)]] float2 fragTexCoord;
    [[vk::location(1)]] float3 fragWorldNormal;
    [[vk::location(2)]] float3 fragWorldTangent;
    [[vk::location(3)]] float3 fragWorldBitangent;
    [[vk::location(4)]] float3 fragWorldPos;
};

float4 getTriplanarColor(float3 worldPos, float3 normal) {
    float3 blendWeights = abs(normal);
    // Tighten the blending to reduce blurring
    blendWeights = pow(blendWeights, float3(4.0));
    blendWeights = blendWeights / (blendWeights.x + blendWeights.y + blendWeights.z);

    float4 colorX = diffuseTexture.Sample(worldPos.zy);
    float4 colorY = diffuseTexture.Sample(worldPos.xz);
    float4 colorZ = diffuseTexture.Sample(worldPos.xy);

    return colorX * blendWeights.x + colorY * blendWeights.y + colorZ * blendWeights.z;
}

[shader("fragment")]
float4 main(FSInput input) : SV_Target {
    float4 diffuseColor;

    if (fragUniforms.useTriplanarMapping != 0u) {
        diffuseColor = getTriplanarColor(input.fragWorldPos, normalize(input.fragWorldNormal));
    } else {
        diffuseColor = diffuseTexture.Sample(input.fragTexCoord);
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
        return float4(applyToneMappingAndGamma(diffuseColor.rgb), diffuseColor.a);
    }

    float3 normalMapSample = normalTexture.Sample(input.fragTexCoord).rgb;
    float3 tangentNormal = normalize(normalMapSample * 2.0 - float3(1.0));

    float3 N = normalize(input.fragWorldNormal);
    float3 T = normalize(input.fragWorldTangent);
    float3 B = normalize(input.fragWorldBitangent);

    float3 perturbedNormal = normalize(
        tangentNormal.x * T +
        tangentNormal.y * B +
        tangentNormal.z * N
    );

    // Key light (directional)
    float3 lightDir = normalize(fragUniforms.lightDir);
    float3 viewDir = normalize(fragUniforms.cameraPos - input.fragWorldPos);
    float keyDiffuse = max(dot(perturbedNormal, lightDir), 0.0);

    // Hemisphere ambient: interpolate between ground and sky based on up-facing
    float hemisphereBlend = dot(perturbedNormal, float3(0.0, 1.0, 0.0)) * 0.5 + 0.5;
    float3 ambientColor = lerp(fragUniforms.hemisphereGroundColor.rgb,
                               fragUniforms.hemisphereSkyColor.rgb,
                               hemisphereBlend);

    // Fill light (diffuse only, no specular)
    float3 fillDir = normalize(fragUniforms.fillLightDir.xyz);
    float fillDiffuse = max(dot(perturbedNormal, fillDir), 0.0) * fragUniforms.fillLightDir.w;

    // Rim light (edge-based, view-dependent)
    float3 rimDir = normalize(fragUniforms.rimLightDir.xyz);
    float rimDot = 1.0 - max(dot(perturbedNormal, viewDir), 0.0);
    float rimFacing = max(dot(perturbedNormal, rimDir), 0.0);
    float rimContrib = pow(rimDot, 3.0) * rimFacing * fragUniforms.rimLightDir.w;

    // Specular (key light only)
    float specularStrength = clamp(fragUniforms.specularStrength, 0.0, 1.0);
    float specularShininess = clamp(fragUniforms.shininess, 8.0, 256.0);
    if (fragUniforms.useDiffuseAlphaAsLuster != 0u) {
        specularStrength = max(specularStrength, sampledAlpha);
        specularShininess = max(specularShininess, lerp(12.0, 160.0, sampledAlpha));
    }

    float specularIntensity = 0.0;
    if (keyDiffuse > 0.0 && specularStrength > 0.0) {
        float3 halfVector = normalize(lightDir + viewDir);
        float specAngle = max(dot(perturbedNormal, halfVector), 0.0);
        float fresnel = pow(1.0 - max(dot(perturbedNormal, viewDir), 0.0), 5.0);
        specularIntensity = pow(specAngle, specularShininess) * specularStrength * (0.08 + 0.92 * fresnel);
    }

    float3 finalColor = diffuseColor.rgb * (keyDiffuse + fillDiffuse)
                      + diffuseColor.rgb * ambientColor
                      + float3(specularIntensity + rimContrib);

    return float4(applyToneMappingAndGamma(finalColor), diffuseColor.a);
}
