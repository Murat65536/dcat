// Vertex shader

struct Uniforms {
    mvp: mat4x4<f32>,
    model: mat4x4<f32>,
};

@group(0) @binding(0)
var<uniform> uniforms: Uniforms;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) texcoord: vec2<f32>,
    @location(2) normal: vec3<f32>,
    @location(3) tangent: vec3<f32>,
    @location(4) bitangent: vec3<f32>,
};

struct VertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) texcoord: vec2<f32>,
    @location(1) world_normal: vec3<f32>,
    @location(2) world_tangent: vec3<f32>,
    @location(3) world_bitangent: vec3<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    
    // Transform position to clip space
    output.clip_position = uniforms.mvp * vec4<f32>(input.position, 1.0);
    
    // Pass through texture coordinates
    output.texcoord = input.texcoord;
    
    // Transform tangent space basis to world space
    output.world_normal = (uniforms.model * vec4<f32>(input.normal, 0.0)).xyz;
    output.world_tangent = (uniforms.model * vec4<f32>(input.tangent, 0.0)).xyz;
    output.world_bitangent = (uniforms.model * vec4<f32>(input.bitangent, 0.0)).xyz;
    
    return output;
}

// Fragment shader

@group(0) @binding(1)
var texture_sampler: sampler;

@group(0) @binding(2)
var diffuse_texture: texture_2d<f32>;

@group(0) @binding(3)
var normal_texture: texture_2d<f32>;

struct FragmentUniforms {
    light_dir: vec3<f32>,
    enable_lighting: u32,
};

@group(0) @binding(4)
var<uniform> fragment_uniforms: FragmentUniforms;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    // Sample diffuse texture
    let diffuse_color = textureSample(diffuse_texture, texture_sampler, input.texcoord);
    
    // If lighting is disabled, return diffuse color directly
    if (fragment_uniforms.enable_lighting == 0u) {
        return vec4<f32>(diffuse_color.rgb, 1.0);
    }
    
    // Sample normal map
    let normal_map_sample = textureSample(normal_texture, texture_sampler, input.texcoord);
    
    // Convert normal map from [0, 1] to [-1, 1] and normalize in one step
    // This is more efficient than separate operations
    let tangent_normal = normalize(normal_map_sample.rgb * 2.0 - vec3<f32>(1.0));
    
    // Normalize interpolated tangent space basis vectors
    // These need normalization because interpolation can change their length
    let N = normalize(input.world_normal);
    let T = normalize(input.world_tangent);
    let B = normalize(input.world_bitangent);
    
    // Transform tangent-space normal to world space using TBN matrix
    // This is already optimized - matrix multiply is done component-wise
    let perturbed_normal = normalize(
        tangent_normal.x * T +
        tangent_normal.y * B +
        tangent_normal.z * N
    );
    
    // Calculate lighting with perturbed normal
    // Light direction is pre-normalized on CPU, no need to normalize here
    // Clamp to minimum ambient value of 0.2 for visibility
    let light_intensity = max(dot(perturbed_normal, fragment_uniforms.light_dir), 0.2);
    
    // Apply lighting to diffuse color
    let final_color = diffuse_color.rgb * light_intensity;
    
    return vec4<f32>(final_color, 1.0);
}
