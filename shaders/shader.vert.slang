// Push constants for frequently updated per-draw data
struct PushConstants {
    float4x4 mvp;
    float4x4 model;
};
[[vk::push_constant]] PushConstants pushConstants;

// Uniform buffer for bone animation data (less frequently updated)
struct Uniforms {
    float4x4 boneMatrices[200];
    uint hasAnimation;
};
[[vk::binding(0, 0)]] ConstantBuffer<Uniforms> uniforms;

struct VSInput {
    [[vk::location(0)]] float3 inPosition;
    [[vk::location(1)]] float2 inTexCoord;
    [[vk::location(2)]] float3 inNormal;
    [[vk::location(3)]] float3 inTangent;
    [[vk::location(4)]] float3 inBitangent;
    [[vk::location(5)]] int4 inBoneIDs;
    [[vk::location(6)]] float4 inBoneWeights;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 fragTexCoord;
    [[vk::location(1)]] float3 fragWorldNormal;
    [[vk::location(2)]] float3 fragWorldTangent;
    [[vk::location(3)]] float3 fragWorldBitangent;
    [[vk::location(4)]] float3 fragWorldPos;
};

[shader("vertex")]
VSOutput main(VSInput input) {
    VSOutput output;

    float4 localPosition;
    float3 localNormal;
    float3 localTangent;
    float3 localBitangent;

    if (uniforms.hasAnimation == 1u) {
        // GPU skinning
        float4x4 boneTransform = (float4x4)0;
        for (int i = 0; i < 4; i++) {
            if (input.inBoneIDs[i] >= 0 && input.inBoneIDs[i] < 200) {
                boneTransform += uniforms.boneMatrices[input.inBoneIDs[i]] * input.inBoneWeights[i];
            }
        }

        localPosition = mul(boneTransform, float4(input.inPosition, 1.0));
        localNormal = mul((float3x3)boneTransform, input.inNormal);
        localTangent = mul((float3x3)boneTransform, input.inTangent);
        localBitangent = mul((float3x3)boneTransform, input.inBitangent);
    } else {
        // Static model
        localPosition = float4(input.inPosition, 1.0);
        localNormal = input.inNormal;
        localTangent = input.inTangent;
        localBitangent = input.inBitangent;
    }

    output.position = mul(pushConstants.mvp, localPosition);
    output.fragTexCoord = input.inTexCoord;
    output.fragWorldNormal = mul(pushConstants.model, float4(localNormal, 0.0)).xyz;
    output.fragWorldTangent = mul(pushConstants.model, float4(localTangent, 0.0)).xyz;
    output.fragWorldBitangent = mul(pushConstants.model, float4(localBitangent, 0.0)).xyz;
    output.fragWorldPos = mul(pushConstants.model, localPosition).xyz;
    return output;
}
