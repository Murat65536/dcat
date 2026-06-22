[[vk::binding(0, 0)]] Sampler2D skyTexture;

struct FSInput {
    [[vk::location(0)]] float2 fragTexCoord;
};

[shader("fragment")]
float4 main(FSInput input) : SV_Target {
    float4 skyColor = skyTexture.Sample(input.fragTexCoord);
    return float4(skyColor.rgb, 1.0);
}
