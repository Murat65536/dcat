use std::{
    collections::HashMap,
    error::Error,
    ops::Range,
    path::{Path, PathBuf},
};

use asset_importer::{
    AABB, Importer, Scene,
    material::{Material as SceneMaterial, TextureType},
    node::Node,
    postprocess::PostProcessSteps,
    texture::TextureDataRef,
};
use bytemuck::{Pod, Zeroable};
use glam::{Mat3, Mat4, Vec3};

use crate::texture::TextureImage;

#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct Vertex {
    pub position: [f32; 3],
    pub uv: [f32; 2],
    pub normal: [f32; 3],
    pub tangent: [f32; 3],
    pub bitangent: [f32; 3],
}

// Per-instance transforms for one placement of a mesh in the scene
#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct Instance {
    pub model: [[f32; 4]; 4],
    // Inverse transpose of the model matrix, for transforming normals
    pub normal: [[f32; 3]; 3],
}

// A mesh drawn with one material at every place it appears in the scene
pub struct MeshBatch {
    pub index_range: Range<u32>,
    pub base_vertex: i32,
    pub instance_range: Range<u32>,
    pub material: usize,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AlphaMode {
    Opaque,
    Mask,
    Blend,
}

pub struct Material {
    pub base_color: [f32; 4],
    pub alpha_mode: AlphaMode,
    pub alpha_cutoff: f32,
    pub specular_strength: f32,
    pub shininess: f32,
    // Indices into ModelData::textures
    pub diffuse: usize,
    pub normal: usize,
}

impl Default for Material {
    fn default() -> Self {
        Self {
            base_color: [1.0; 4],
            alpha_mode: AlphaMode::Opaque,
            alpha_cutoff: 0.5,
            specular_strength: 0.0,
            shininess: 32.0,
            diffuse: 0,
            normal: 0,
        }
    }
}

pub struct ModelTexture {
    pub image: TextureImage,
    // Color data is stored in sRGB; normal maps are linear
    pub srgb: bool,
}

// Textures given on the command line, replacing those of every material
#[derive(Clone, Copy, Default)]
pub struct TextureOverrides<'a> {
    pub diffuse: Option<&'a Path>,
    pub normal: Option<&'a Path>,
}

// A model loaded into CPU memory, ready for upload
pub struct ModelData {
    pub vertices: Vec<Vertex>,
    pub indices: Vec<u32>,
    pub instances: Vec<Instance>,
    // Bounding-box center of each instance in model space, for sorting
    // blended draws by distance
    pub instance_centers: Vec<Vec3>,
    pub batches: Vec<MeshBatch>,
    pub materials: Vec<Material>,
    pub textures: Vec<ModelTexture>,
    pub bounds: AABB,
    // Rotation that brings the file's up axis to +Y
    pub coordinate_system: Mat4,
    // False when no mesh has texture coordinates, so textures are projected instead
    pub has_uvs: bool,
}

impl ModelData {
    pub fn load(path: &Path, overrides: TextureOverrides) -> Result<Self, Box<dyn Error>> {
        let scene = Importer::new().import_file_with(path.as_os_str(), |b| {
            b.with_post_process(
                PostProcessSteps::TRIANGULATE
                    | PostProcessSteps::GEN_NORMALS
                    | PostProcessSteps::CALC_TANGENT_SPACE
                    | PostProcessSteps::JOIN_IDENTICAL_VERTICES
                    | PostProcessSteps::SORT_BY_PTYPE
                    | PostProcessSteps::FLIP_UVS
                    | PostProcessSteps::GEN_BOUNDING_BOXES,
            )
        })?;

        let model_dir = path.parent().unwrap_or(Path::new(""));
        let mut textures = TextureLoader::new(&scene);
        let materials = load_materials(&scene, model_dir, overrides, &mut textures);
        let uv_channel = diffuse_uv_channel(&scene);

        let mut vertices = Vec::<Vertex>::new();
        let mut indices = Vec::<u32>::new();
        let mut batches = Vec::<MeshBatch>::new();
        let mut scene_to_gpu = vec![None; scene.num_meshes()];
        let mut has_uvs = false;

        for (scene_index, gpu_slot) in scene_to_gpu.iter_mut().enumerate() {
            let Some(mesh) = scene.mesh(scene_index) else {
                continue;
            };

            // Points and lines are split into their own meshes and skipped
            if !mesh.has_vertices() || !mesh.has_triangles() {
                continue;
            }

            let mesh_indices = mesh.triangle_indices_iter().collect::<Vec<u32>>();
            if mesh_indices.is_empty() {
                continue;
            }

            let first_index = u32::try_from(indices.len())?;
            let base_vertex = i32::try_from(vertices.len())?;

            let positions = mesh.vertices_raw();
            let normals = mesh.normals_raw_opt();
            let tangents = mesh.tangents_raw_opt().zip(mesh.bitangents_raw_opt());
            let uvs = mesh.texture_coords_raw_opt(uv_channel);
            has_uvs |= uvs.is_some();

            vertices.extend(positions.iter().enumerate().map(|(i, position)| {
                let (tangent, bitangent) = tangents
                    .map_or(([1.0, 0.0, 0.0], [0.0, 0.0, 1.0]), |(t, b)| {
                        ([t[i].x, t[i].y, t[i].z], [b[i].x, b[i].y, b[i].z])
                    });
                Vertex {
                    position: [position.x, position.y, position.z],
                    uv: uvs.map_or([0.0, 0.0], |uv| [uv[i].x, uv[i].y]),
                    normal: normals.map_or([0.0, 1.0, 0.0], |n| [n[i].x, n[i].y, n[i].z]),
                    tangent,
                    bitangent,
                }
            }));
            indices.extend_from_slice(&mesh_indices);

            *gpu_slot = Some(batches.len());
            batches.push(MeshBatch {
                index_range: first_index..u32::try_from(indices.len())?,
                base_vertex,
                instance_range: 0..0,
                material: mesh.material_index().min(materials.len() - 1),
            });
        }

        if vertices.is_empty() {
            return Err(format!("No triangle meshes found in {}", path.display()).into());
        }

        let mut instance_batches = vec![Vec::<(Instance, Vec3)>::new(); batches.len()];
        let mut bounds = AABB::empty();

        if let Some(root) = scene.root_node() {
            visit_node(
                &scene,
                &root,
                Mat4::IDENTITY,
                &scene_to_gpu,
                &mut instance_batches,
                &mut bounds,
            );
        }

        let instance_count = instance_batches.iter().map(Vec::len).sum();
        let mut instances = Vec::with_capacity(instance_count);
        let mut instance_centers = Vec::with_capacity(instance_count);
        for (batch, mesh_instances) in batches.iter_mut().zip(instance_batches) {
            let first_instance = u32::try_from(instances.len())?;
            for (instance, center) in mesh_instances {
                instances.push(instance);
                instance_centers.push(center);
            }
            batch.instance_range = first_instance..u32::try_from(instances.len())?;
        }

        let coordinate_system = scene.metadata().map_or(Mat4::IDENTITY, |metadata| {
            up_axis_conversion(
                metadata.get_i32("UpAxis").unwrap_or(1),
                metadata.get_i32("UpAxisSign").unwrap_or(1),
            )
        });

        Ok(Self {
            vertices,
            indices,
            instances,
            instance_centers,
            batches,
            materials,
            textures: textures.finish(),
            bounds,
            coordinate_system,
            has_uvs,
        })
    }
}

fn visit_node(
    scene: &Scene,
    node: &Node,
    parent_world: Mat4,
    scene_to_gpu: &[Option<usize>],
    instance_batches: &mut [Vec<(Instance, Vec3)>],
    bounds: &mut AABB,
) {
    let local: Mat4 = node.transformation().into();
    let world = parent_world * local;

    let normal_matrix = Mat3::from_mat4(world).inverse().transpose();
    // A degenerate transform has no inverse; its normals are meaningless anyway
    let normal_matrix = if normal_matrix.is_finite() {
        normal_matrix
    } else {
        Mat3::from_mat4(world)
    };

    for scene_index in node.mesh_indices_iter() {
        let Some(gpu_index) = scene_to_gpu.get(scene_index).copied().flatten() else {
            continue;
        };

        let Some(mesh) = scene.mesh(scene_index) else {
            continue;
        };
        let world_bounds = mesh.aabb().transformed(&world.into());
        bounds.expand_to_include_aabb(&world_bounds);

        instance_batches[gpu_index].push((
            Instance {
                model: world.to_cols_array_2d(),
                normal: normal_matrix.to_cols_array_2d(),
            },
            Vec3::from(world_bounds.center()),
        ));
    }

    for child in node.children() {
        visit_node(scene, &child, world, scene_to_gpu, instance_batches, bounds);
    }
}

// Rotation that brings the declared up axis (assimp's UpAxis: 0 = X, 1 = Y,
// 2 = Z) with the given sign (UpAxisSign: +1 or -1) onto +Y
fn up_axis_conversion(axis: i32, sign: i32) -> Mat4 {
    let sign = if sign < 0 { -1.0 } else { 1.0 };
    let quarter_turn = 90f32.to_radians();
    match axis {
        0 => Mat4::from_rotation_z(sign * quarter_turn),
        2 => Mat4::from_rotation_x(-sign * quarter_turn),
        // Y, or an unknown axis treated as Y
        _ if sign < 0.0 => Mat4::from_rotation_x(std::f32::consts::PI),
        _ => Mat4::IDENTITY,
    }
}

// The UV channel the diffuse textures are mapped with
fn diffuse_uv_channel(scene: &Scene) -> usize {
    scene
        .materials()
        .filter_map(|material| {
            material
                .texture(TextureType::BaseColor, 0)
                .or_else(|| material.texture(TextureType::Diffuse, 0))
        })
        .map(|texture| texture.uv_index as usize)
        .find(|&channel| channel > 0)
        .unwrap_or(0)
}

fn load_materials(
    scene: &Scene,
    model_dir: &Path,
    overrides: TextureOverrides,
    textures: &mut TextureLoader,
) -> Vec<Material> {
    let texture_source = |material: &SceneMaterial, types: [TextureType; 2]| {
        types
            .into_iter()
            .find_map(|texture_type| material.texture(texture_type, 0))
            .and_then(|texture| resolve_texture_path(model_dir, &texture.path, scene))
    };
    let override_source = |path: Option<&Path>| path.map(TextureSource::from_argument);

    let mut materials: Vec<Material> = scene
        .materials()
        .map(|material| {
            let diffuse = override_source(overrides.diffuse).or_else(|| {
                texture_source(&material, [TextureType::Diffuse, TextureType::BaseColor])
            });
            let normal = override_source(overrides.normal)
                .or_else(|| texture_source(&material, [TextureType::Normals, TextureType::Height]));

            Material {
                diffuse: textures.load(diffuse, TextureRole::Diffuse),
                normal: textures.load(normal, TextureRole::Normal),
                ..material_properties(&material)
            }
        })
        .collect();

    if materials.is_empty() {
        materials.push(Material {
            diffuse: textures.load(override_source(overrides.diffuse), TextureRole::Diffuse),
            normal: textures.load(override_source(overrides.normal), TextureRole::Normal),
            ..Material::default()
        });
    }
    materials
}

// Translate the file's material parameters into the Blinn-Phong inputs the shader uses
fn material_properties(material: &SceneMaterial) -> Material {
    let mut result = Material::default();

    if let Some(color) = material.base_color().or_else(|| {
        material
            .diffuse_color()
            .map(|c| asset_importer::Color4D::new(c.x, c.y, c.z, 1.0))
    }) {
        result.base_color = [color.x, color.y, color.z, color.w];
    }

    result.alpha_mode = match material
        .get_string_property_str("$mat.gltf.alphaMode")
        .ok()
        .flatten()
        .as_deref()
    {
        Some("MASK") => AlphaMode::Mask,
        Some("BLEND") => AlphaMode::Blend,
        _ if material.opacity().is_some_and(|opacity| opacity < 1.0) => AlphaMode::Blend,
        _ if material.texture_count(TextureType::Opacity) > 0 => AlphaMode::Blend,
        _ => AlphaMode::Opaque,
    };
    if let Ok(Some(cutoff)) = material.get_float_property_str("$mat.gltf.alphaCutoff") {
        result.alpha_cutoff = cutoff;
    }

    let mut specular = 0.0f32;
    if let Some(shininess) = material.shininess().filter(|&s| s > 0.0) {
        result.shininess = shininess.clamp(8.0, 256.0);
        specular = specular.max(0.2);
    }
    for strength in [
        material.shininess_strength(),
        material.reflectivity(),
        material
            .specular_color()
            .map(|color| color.x.max(color.y).max(color.z)),
        material.metallic_factor(),
    ]
    .into_iter()
    .flatten()
    {
        specular = specular.max(strength.clamp(0.0, 1.0));
    }
    if let Some(roughness) = material.roughness_factor() {
        let smoothness = 1.0 - roughness.clamp(0.0, 1.0);
        result.shininess = (smoothness * 120.0 + 8.0).clamp(8.0, 256.0);
        specular = specular.max(smoothness * 0.35);
    }
    result.specular_strength = specular;

    result
}

#[derive(Clone, Debug, PartialEq, Eq, Hash)]
enum TextureSource {
    File(PathBuf),
    // Index into the scene's embedded textures
    Embedded(usize),
}

impl TextureSource {
    // "*N" refers to embedded texture N, anything else is a file
    fn parse_embedded(path: &str) -> Option<Self> {
        path.strip_prefix('*')?.parse().ok().map(Self::Embedded)
    }

    fn from_argument(path: &Path) -> Self {
        path.to_str()
            .and_then(Self::parse_embedded)
            .unwrap_or_else(|| Self::File(path.to_path_buf()))
    }
}

fn file_name(path: &str) -> &str {
    path.rsplit(['/', '\\']).next().unwrap_or(path)
}

// Find the texture a material refers to. Exporters often store paths from the
// authoring machine, so embedded textures are matched by file name and absolute
// paths that don't exist are looked up next to the model instead.
fn resolve_texture_path(model_dir: &Path, path: &str, scene: &Scene) -> Option<TextureSource> {
    if path.is_empty() {
        return None;
    }
    if let Some(source) = TextureSource::parse_embedded(path) {
        return Some(source);
    }

    let name = file_name(path);
    if let Some(index) = scene.textures().position(|texture| {
        texture
            .filename_str()
            .is_some_and(|embedded| embedded == path || file_name(&embedded) == name)
    }) {
        return Some(TextureSource::Embedded(index));
    }

    Some(TextureSource::File(resolve_file_path(model_dir, path)))
}

fn resolve_file_path(model_dir: &Path, path: &str) -> PathBuf {
    let bytes = path.as_bytes();
    let is_windows_absolute =
        bytes.len() >= 3 && bytes[1] == b':' && matches!(bytes[2], b'\\' | b'/');

    let as_path = Path::new(path);
    if as_path.is_absolute() && as_path.exists() {
        return as_path.to_path_buf();
    }
    if as_path.is_absolute() || is_windows_absolute {
        return model_dir.join(file_name(path));
    }

    // Relative paths written on Windows use backslashes
    let relative = if cfg!(windows) {
        path.to_owned()
    } else {
        path.replace('\\', "/")
    };
    model_dir.join(relative)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
enum TextureRole {
    Diffuse,
    Normal,
}

// Decodes each texture once, however many materials share it
struct TextureLoader<'a> {
    scene: &'a Scene,
    textures: Vec<ModelTexture>,
    cache: HashMap<(Option<TextureSource>, TextureRole), usize>,
}

impl<'a> TextureLoader<'a> {
    fn new(scene: &'a Scene) -> Self {
        Self {
            scene,
            textures: Vec::new(),
            cache: HashMap::new(),
        }
    }

    fn load(&mut self, source: Option<TextureSource>, role: TextureRole) -> usize {
        let key = (source, role);
        if let Some(&index) = self.cache.get(&key) {
            return index;
        }

        let fallback = match role {
            TextureRole::Diffuse => TextureImage::default_diffuse,
            TextureRole::Normal => TextureImage::flat_normal,
        };
        let image = match &key.0 {
            None => fallback(),
            Some(source) => self.decode(source).unwrap_or_else(|error| {
                eprintln!("Warning: {error}, using a default texture");
                fallback()
            }),
        };

        let index = self.textures.len();
        self.textures.push(ModelTexture {
            image,
            srgb: role == TextureRole::Diffuse,
        });
        self.cache.insert(key, index);
        index
    }

    fn decode(&self, source: &TextureSource) -> Result<TextureImage, String> {
        match source {
            TextureSource::File(path) => TextureImage::from_file(path)
                .map_err(|error| format!("failed to load texture {}: {error}", path.display())),
            TextureSource::Embedded(index) => {
                let texture = self
                    .scene
                    .texture(*index)
                    .ok_or_else(|| format!("embedded texture *{index} does not exist"))?;
                let data = texture.data_ref().map_err(|error| {
                    format!("failed to read embedded texture *{index}: {error}")
                })?;
                match data {
                    TextureDataRef::Compressed(bytes) => {
                        TextureImage::from_memory(bytes).map_err(|error| {
                            format!("failed to decode embedded texture *{index}: {error}")
                        })
                    }
                    TextureDataRef::Texels(texels) => Ok(TextureImage {
                        width: texture.width(),
                        height: texture.height(),
                        pixels: texels.iter().flat_map(|t| [t.r, t.g, t.b, t.a]).collect(),
                    }),
                }
            }
        }
    }

    fn finish(self) -> Vec<ModelTexture> {
        self.textures
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const SPOT: &str = "tests/fixtures/spot";

    fn load(name: &str) -> ModelData {
        load_with(name, TextureOverrides::default())
    }

    fn load_with(name: &str, overrides: TextureOverrides) -> ModelData {
        let path = Path::new(SPOT).join(name);
        ModelData::load(&path, overrides).unwrap_or_else(|e| panic!("{name}: {e}"))
    }

    fn assert_valid_geometry(model: &ModelData) {
        assert!(!model.vertices.is_empty());
        assert!(!model.indices.is_empty());
        assert_eq!(model.indices.len() % 3, 0);
        assert!(!model.batches.is_empty());
        assert!(!model.instances.is_empty());

        // Every index must point at a real vertex
        for batch in &model.batches {
            for &index in
                &model.indices[batch.index_range.start as usize..batch.index_range.end as usize]
            {
                let vertex = batch.base_vertex as usize + index as usize;
                assert!(vertex < model.vertices.len());
            }
            assert!(batch.material < model.materials.len());
        }
        for material in &model.materials {
            assert!(material.diffuse < model.textures.len());
            assert!(material.normal < model.textures.len());
        }
    }

    #[test]
    fn triangulated_geometry() {
        let model = load("spot_triangulated.obj");
        assert_valid_geometry(&model);
        assert!(model.has_uvs);
        assert!(!model.bounds.is_empty());

        // Normals were generated and are unit length
        for vertex in &model.vertices {
            let length = glam::Vec3::from(vertex.normal).length();
            assert!((length - 1.0).abs() < 1e-3, "normal length {length}");
        }
    }

    #[test]
    fn quads_are_triangulated() {
        let model = load("spot_quadrangulated.obj");
        assert_valid_geometry(&model);
        assert!(model.has_uvs);
    }

    #[test]
    fn control_mesh_is_coarse() {
        let model = load("spot_control_mesh.obj");
        assert_valid_geometry(&model);
        assert!(model.indices.len() < 5000);
    }

    #[test]
    fn missing_file_fails() {
        assert!(
            ModelData::load(Path::new("does_not_exist.obj"), TextureOverrides::default()).is_err()
        );
    }

    #[test]
    fn untextured_material_uses_defaults() {
        let model = load("spot_triangulated.obj");
        let material = &model.materials[model.batches[0].material];
        assert_eq!(
            model.textures[material.diffuse].image,
            TextureImage::default_diffuse()
        );
        assert_eq!(
            model.textures[material.normal].image,
            TextureImage::flat_normal()
        );
        assert!(model.textures[material.diffuse].srgb);
        assert!(!model.textures[material.normal].srgb);
    }

    #[test]
    fn texture_override_applies_to_every_material() {
        let texture = Path::new(SPOT).join("spot_texture.png");
        let model = load_with(
            "spot_triangulated.obj",
            TextureOverrides {
                diffuse: Some(&texture),
                normal: None,
            },
        );
        let expected = TextureImage::from_file(&texture).unwrap();
        for material in &model.materials {
            assert_eq!(model.textures[material.diffuse].image, expected);
        }
    }

    #[test]
    fn missing_override_falls_back() {
        let model = load_with(
            "spot_triangulated.obj",
            TextureOverrides {
                diffuse: Some(Path::new("missing.png")),
                normal: Some(Path::new("missing_normal.png")),
            },
        );
        let material = &model.materials[0];
        assert_eq!(
            model.textures[material.diffuse].image,
            TextureImage::default_diffuse()
        );
        assert_eq!(
            model.textures[material.normal].image,
            TextureImage::flat_normal()
        );
    }

    #[test]
    fn gltf_materials_and_embedded_texture() {
        let model = ModelData::load(
            Path::new("tests/fixtures/gltf/alpha_modes.gltf"),
            TextureOverrides::default(),
        )
        .unwrap();
        assert_valid_geometry(&model);
        assert!(model.has_uvs);

        let material = |name_index: usize| {
            let batch = &model.batches[name_index];
            &model.materials[batch.material]
        };
        let masked = material(0);
        let blended = material(1);

        assert_eq!(masked.alpha_mode, AlphaMode::Mask);
        assert!((masked.alpha_cutoff - 0.25).abs() < 1e-6);
        assert_eq!(masked.base_color, [1.0, 0.5, 0.25, 1.0]);
        // Roughness 0.5 maps to a moderate highlight. Assimp also derives a
        // shininess from roughness, which sets the 0.2 specular floor.
        assert!((masked.shininess - 68.0).abs() < 1e-3);
        assert!((masked.specular_strength - 0.2).abs() < 1e-6);

        assert_eq!(blended.alpha_mode, AlphaMode::Blend);
        // Fully metallic surfaces get the strongest highlight
        assert_eq!(blended.specular_strength, 1.0);

        // Both materials share the one embedded texture, decoded once
        assert_eq!(masked.diffuse, blended.diffuse);
        let texture = &model.textures[masked.diffuse].image;
        assert_eq!((texture.width, texture.height), (2, 2));
        assert_eq!(&texture.pixels[0..4], &[255, 0, 0, 255]);
        assert!(texture.has_transparency());
    }

    #[test]
    fn embedded_references() {
        assert_eq!(
            TextureSource::parse_embedded("*3"),
            Some(TextureSource::Embedded(3))
        );
        assert_eq!(TextureSource::parse_embedded("*x"), None);
        assert_eq!(TextureSource::parse_embedded("tex.png"), None);
        assert_eq!(
            TextureSource::from_argument(Path::new("*0")),
            TextureSource::Embedded(0)
        );
    }

    #[test]
    fn file_paths_resolve_next_to_model() {
        let dir = Path::new("models");
        assert_eq!(resolve_file_path(dir, "tex.png"), dir.join("tex.png"));
        assert_eq!(
            resolve_file_path(dir, r"C:\Users\artist\tex.png"),
            dir.join("tex.png")
        );
        assert_eq!(
            resolve_file_path(dir, "/home/artist/missing/tex.png"),
            dir.join("tex.png")
        );
        if cfg!(unix) {
            assert_eq!(
                resolve_file_path(dir, r"textures\tex.png"),
                dir.join("textures/tex.png")
            );
        }
    }

    #[test]
    fn up_axis_with_sign() {
        use glam::Vec3;
        for (axis, sign, declared_up) in [
            (0, 1, Vec3::X),
            (0, -1, Vec3::NEG_X),
            (1, 1, Vec3::Y),
            (1, -1, Vec3::NEG_Y),
            (2, 1, Vec3::Z),
            (2, -1, Vec3::NEG_Z),
        ] {
            let up = up_axis_conversion(axis, sign).transform_vector3(declared_up);
            assert!(
                up.abs_diff_eq(Vec3::Y, 1e-6),
                "axis {axis} sign {sign}: {up}"
            );
        }
    }

    #[test]
    fn up_axis() {
        let z_up = up_axis_conversion(2, 1);
        assert!(
            z_up.transform_vector3(glam::Vec3::Z)
                .abs_diff_eq(glam::Vec3::Y, 1e-6)
        );
        let x_up = up_axis_conversion(0, 1);
        assert!(
            x_up.transform_vector3(glam::Vec3::X)
                .abs_diff_eq(glam::Vec3::Y, 1e-6)
        );
        assert_eq!(up_axis_conversion(1, 1), Mat4::IDENTITY);
    }
}
