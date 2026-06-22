module common;

// Shared post-processing used by fragment shaders.
public float3 applyToneMappingAndGamma(float3 hdrColor) {
    const float exposure = 1.0;
    const float gamma = 2.2;

    // Simple HDR-style exposure tone mapping.
    float3 mapped = float3(1.0) - exp(-max(hdrColor, float3(0.0)) * exposure);
    // Final output gamma encoding for display.
    return pow(mapped, float3(1.0 / gamma));
}
