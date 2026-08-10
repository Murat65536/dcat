struct CameraUniform {
    transform: mat4x4<f32>,
}

@group(0) @binding(0) var<uniform> camera_uniform: CameraUniform;

@vertex
fn vs_main(@location(0) position: vec3<f32>) -> @builtin(position) vec4<f32> {
    return camera_uniform.transform * vec4<f32>(position, 1);
}

@fragment
fn fs_main() -> @location(0) vec4<f32> {
    return vec4<f32>(1.0, 0.0, 0.0, 1.0);
}
