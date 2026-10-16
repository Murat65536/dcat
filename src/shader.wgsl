struct FrameUniforms {
    view_projection: mat4x4<f32>,
    // Whole-model transform (framing, spin, up axis)
    model: mat4x4<f32>,
    camera_position: vec3<f32>,
    enable_lighting: u32,
    // Direction towards the key light
    light_direction: vec3<f32>,
    use_triplanar_mapping: u32,
    hemisphere_sky_color: vec4<f32>,
    hemisphere_ground_color: vec4<f32>,
    // xyz = direction, w = intensity
    fill_light: vec4<f32>,
    rim_light: vec4<f32>,
}

struct MaterialUniforms {
    base_color: vec4<f32>,
    // 0 = opaque, 1 = mask, 2 = blend
    alpha_mode: u32,
    alpha_cutoff: f32,
    specular_strength: f32,
    shininess: f32,
    use_diffuse_alpha_as_luster: u32,
}

@group(0) @binding(0) var<uniform> frame: FrameUniforms;

@group(1) @binding(0) var<uniform> material: MaterialUniforms;
@group(1) @binding(1) var diffuse_texture: texture_2d<f32>;
@group(1) @binding(2) var normal_texture: texture_2d<f32>;
@group(1) @binding(3) var texture_sampler: sampler;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) normal: vec3<f32>,
    @location(3) tangent: vec3<f32>,
    @location(4) bitangent: vec3<f32>,
}

struct InstanceInput {
    @location(5) model_0: vec4<f32>,
    @location(6) model_1: vec4<f32>,
    @location(7) model_2: vec4<f32>,
    @location(8) model_3: vec4<f32>,
    @location(9) normal_0: vec3<f32>,
    @location(10) normal_1: vec3<f32>,
    @location(11) normal_2: vec3<f32>,
}

struct VertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) world_normal: vec3<f32>,
    @location(2) world_tangent: vec3<f32>,
    @location(3) world_bitangent: vec3<f32>,
    @location(4) world_position: vec3<f32>,
}

fn upper_3x3(m: mat4x4<f32>) -> mat3x3<f32> {
    return mat3x3<f32>(m[0].xyz, m[1].xyz, m[2].xyz);
}

@vertex
fn vs_main(vertex: VertexInput, instance: InstanceInput) -> VertexOutput {
    let instance_model = mat4x4<f32>(
        instance.model_0,
        instance.model_1,
        instance.model_2,
        instance.model_3,
    );
    let instance_normal = mat3x3<f32>(instance.normal_0, instance.normal_1, instance.normal_2);
    // The whole-model transform is a rotation and uniform scale, so it can
    // transform normals directly.
    let frame_rotation = upper_3x3(frame.model);
    let instance_rotation = upper_3x3(instance_model);

    let world_position = frame.model * instance_model * vec4<f32>(vertex.position, 1.0);

    var out: VertexOutput;
    out.clip_position = frame.view_projection * world_position;
    out.uv = vertex.uv;
    out.world_normal = frame_rotation * (instance_normal * vertex.normal);
    out.world_tangent = frame_rotation * (instance_rotation * vertex.tangent);
    out.world_bitangent = frame_rotation * (instance_rotation * vertex.bitangent);
    out.world_position = world_position.xyz;
    return out;
}

fn apply_tone_mapping_and_gamma(hdr_color: vec3<f32>) -> vec3<f32> {
    let exposure = 1.0;
    let gamma = 2.2;
    let mapped = vec3<f32>(1.0) - exp(-max(hdr_color, vec3<f32>(0.0)) * exposure);
    return pow(mapped, vec3<f32>(1.0 / gamma));
}

// Project the texture along each world axis and blend by the surface normal,
// for models without texture coordinates
fn triplanar_color(world_position: vec3<f32>, normal: vec3<f32>) -> vec4<f32> {
    var weights = pow(abs(normal), vec3<f32>(4.0));
    weights = weights / (weights.x + weights.y + weights.z);

    let x = textureSample(diffuse_texture, texture_sampler, world_position.zy);
    let y = textureSample(diffuse_texture, texture_sampler, world_position.xz);
    let z = textureSample(diffuse_texture, texture_sampler, world_position.xy);
    return x * weights.x + y * weights.y + z * weights.z;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let geometric_normal = normalize(in.world_normal);

    // Sample everything up front, while control flow is still uniform
    let planar_color = textureSample(diffuse_texture, texture_sampler, in.uv);
    let projected_color = triplanar_color(in.world_position, geometric_normal);
    let normal_sample = textureSample(normal_texture, texture_sampler, in.uv).rgb;

    var diffuse_color = select(planar_color, projected_color, frame.use_triplanar_mapping != 0u);
    diffuse_color *= material.base_color;
    let sampled_alpha = diffuse_color.a;

    if material.alpha_mode == 0u {
        diffuse_color.a = 1.0;
    } else if material.alpha_mode == 1u {
        if diffuse_color.a < material.alpha_cutoff {
            discard;
        }
        diffuse_color.a = 1.0;
    }

    if frame.enable_lighting == 0u {
        return vec4<f32>(apply_tone_mapping_and_gamma(diffuse_color.rgb), diffuse_color.a);
    }

    let tangent_normal = normalize(normal_sample * 2.0 - vec3<f32>(1.0));
    let n = normalize(
        tangent_normal.x * normalize(in.world_tangent)
            + tangent_normal.y * normalize(in.world_bitangent)
            + tangent_normal.z * geometric_normal
    );

    // Key light
    let light_direction = normalize(frame.light_direction);
    let view_direction = normalize(frame.camera_position - in.world_position);
    let key_diffuse = max(dot(n, light_direction), 0.0);

    // Hemisphere ambient: blend from ground to sky as the surface faces up
    let hemisphere_blend = dot(n, vec3<f32>(0.0, 1.0, 0.0)) * 0.5 + 0.5;
    let ambient_color = mix(
        frame.hemisphere_ground_color.rgb,
        frame.hemisphere_sky_color.rgb,
        hemisphere_blend
    );

    // Fill light, diffuse only
    let fill_diffuse = max(dot(n, normalize(frame.fill_light.xyz)), 0.0) * frame.fill_light.w;

    // Rim light on edges facing away from the viewer
    let rim_edge = 1.0 - max(dot(n, view_direction), 0.0);
    let rim_facing = max(dot(n, normalize(frame.rim_light.xyz)), 0.0);
    let rim = pow(rim_edge, 3.0) * rim_facing * frame.rim_light.w;

    // Specular, key light only
    var specular_strength = clamp(material.specular_strength, 0.0, 1.0);
    var shininess = clamp(material.shininess, 8.0, 256.0);
    if material.use_diffuse_alpha_as_luster != 0u {
        specular_strength = max(specular_strength, sampled_alpha);
        shininess = max(shininess, mix(12.0, 160.0, sampled_alpha));
    }

    var specular = 0.0;
    if key_diffuse > 0.0 && specular_strength > 0.0 {
        let half_vector = normalize(light_direction + view_direction);
        let specular_angle = max(dot(n, half_vector), 0.0);
        let fresnel = pow(1.0 - max(dot(n, view_direction), 0.0), 5.0);
        specular = pow(specular_angle, shininess) * specular_strength * (0.08 + 0.92 * fresnel);
    }

    let color = diffuse_color.rgb * (key_diffuse + fill_diffuse)
        + diffuse_color.rgb * ambient_color
        + vec3<f32>(specular + rim);

    return vec4<f32>(apply_tone_mapping_and_gamma(color), diffuse_color.a);
}
