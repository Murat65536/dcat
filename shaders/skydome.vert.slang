struct PushConstants {
    float4x4 mvp;
};
[[vk::push_constant]] PushConstants pushConstants;

struct VSInput {
    [[vk::location(0)]] float3 inPosition;
    [[vk::location(1)]] float2 inTexCoord;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 fragTexCoord;
};

[shader("vertex")]
VSOutput main(VSInput input) {
    VSOutput output;
    float4 pos = mul(pushConstants.mvp, float4(input.inPosition, 1.0));
    // Force depth to far plane so skydome renders behind everything
    output.position = pos.xyww;
    output.fragTexCoord = input.inTexCoord;
    return output;
}
