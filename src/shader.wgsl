struct CameraUniform {
    transform: mat4x4<f32>,
}

struct ObjectUniform {
    transform: mat4x4<f32>,
}

@group(0) @binding(0)
var<uniform> camera_uniform: CameraUniform;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) transform_col_0: vec4<f32>,
    @location(2) transform_col_1: vec4<f32>,
    @location(3) transform_col_2: vec4<f32>,
    @location(4) transform_col_3: vec4<f32>,
}

@vertex
fn vs_main(input: VertexInput) -> @builtin(position) vec4<f32> {
    let model = mat4x4<f32>(
        input.transform_col_0,
        input.transform_col_1,
        input.transform_col_2,
        input.transform_col_3,
    );

    return camera_uniform.transform * model * vec4<f32>(input.position, 1);
}

@fragment
fn fs_main() -> @location(0) vec4<f32> {
    return vec4<f32>(1.0, 0.0, 0.0, 1.0);
}
