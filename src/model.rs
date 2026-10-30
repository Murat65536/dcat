use std::{
    collections::HashMap,
    error::Error,
    ops::Range,
    path::{Path, PathBuf},
};

use asset_importer::{
    AABB, Importer, Scene,
    material::{Material as SceneMaterial, TextureType},
    mesh::Mesh,
    node::Node,
    postprocess::PostProcessSteps,
    texture::TextureDataRef,
};
use bytemuck::{Pod, Zeroable};
use glam::{Mat3, Mat4, Vec3};
use typed_path::{Utf8Component, Utf8WindowsPath};

use crate::{
    animation::{Animation, MAX_BONES, Skeleton},
    texture::TextureImage,
};

#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct Vertex {
    pub position: [f32; 3],
    pub uv: [f32; 2],
    pub normal: [f32; 3],
    pub tangent: [f32; 3],
    pub bitangent: [f32; 3],
    // Up to four bones moving the vertex, used only by animated models
    pub bone_ids: [u32; 4],
    pub bone_weights: [f32; 4],
}

impl Vertex {
    // Where the vertex lands with the given bone matrices
    pub fn skinned_position(&self, bones: &[Mat4]) -> Vec3 {
        let skin: Mat4 = self
            .bone_ids
            .iter()
            .zip(self.bone_weights)
            .filter(|&(_, weight)| weight > 0.0)
            .map(|(&id, weight)| bones.get(id as usize).copied().unwrap_or(Mat4::IDENTITY) * weight)
            .fold(Mat4::ZERO, |sum, matrix| sum + matrix);
        skin.transform_point3(Vec3::from(self.position))
    }

    // Add a bone influence, replacing the weakest if all four slots are used
    fn add_bone(&mut self, bone: usize, weight: f32) {
        let slot = (0..4)
            .min_by(|&a, &b| self.bone_weights[a].total_cmp(&self.bone_weights[b]))
            .unwrap_or(0);
        if weight > self.bone_weights[slot] {
            self.bone_ids[slot] = bone as u32;
            self.bone_weights[slot] = weight;
        }
    }
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

// The skeleton and animations of an animated model
pub struct AnimationData {
    pub skeleton: Skeleton,
    pub animations: Vec<Animation>,
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
    // Set for animated models, whose vertices are posed by bones
    pub animation: Option<AnimationData>,
}

// Meshes laid out for drawing
struct Geometry {
    vertices: Vec<Vertex>,
    indices: Vec<u32>,
    instances: Vec<Instance>,
    instance_centers: Vec<Vec3>,
    batches: Vec<MeshBatch>,
    bounds: AABB,
    has_uvs: bool,
    animation: Option<AnimationData>,
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
                    | PostProcessSteps::GEN_BOUNDING_BOXES
                    // At most four bones per vertex, as the shader expects
                    | PostProcessSteps::LIMIT_BONE_WEIGHTS,
            )
        })?;

        let model_dir = path.parent().unwrap_or(Path::new(""));
        let mut textures = TextureLoader::new(&scene);
        let materials = load_materials(&scene, model_dir, overrides, &mut textures);
        let uv_channel = diffuse_uv_channel(&scene);

        let geometry = if scene.num_animations() > 0 {
            match animated_geometry(&scene, uv_channel, materials.len())? {
                Some(geometry) => geometry,
                None => static_geometry(&scene, uv_channel, materials.len())?,
            }
        } else {
            static_geometry(&scene, uv_channel, materials.len())?
        };
        if geometry.vertices.is_empty() {
            return Err(format!("No triangle meshes found in {}", path.display()).into());
        }

        let coordinate_system = scene.metadata().map_or(Mat4::IDENTITY, |metadata| {
            up_axis_conversion(
                metadata.get_i32("UpAxis").unwrap_or(1),
                metadata.get_i32("UpAxisSign").unwrap_or(1),
            )
        });

        Ok(Self {
            vertices: geometry.vertices,
            indices: geometry.indices,
            instances: geometry.instances,
            instance_centers: geometry.instance_centers,
            batches: geometry.batches,
            materials,
            textures: textures.finish(),
            bounds: geometry.bounds,
            coordinate_system,
            has_uvs: geometry.has_uvs,
            animation: geometry.animation,
        })
    }
}

// A triangle mesh's vertices in mesh space, without bones. None for meshes
// that aren't triangles, such as points and lines.
fn mesh_vertices(mesh: &Mesh, uv_channel: usize) -> Option<(Vec<Vertex>, Vec<u32>, bool)> {
    if !mesh.has_vertices() || !mesh.has_triangles() {
        return None;
    }
    let indices: Vec<u32> = mesh.triangle_indices_iter().collect();
    if indices.is_empty() {
        return None;
    }

    let normals = mesh.normals_raw_opt();
    let tangents = mesh.tangents_raw_opt().zip(mesh.bitangents_raw_opt());
    let uvs = mesh.texture_coords_raw_opt(uv_channel);
    let vertices = mesh
        .vertices_raw()
        .iter()
        .enumerate()
        .map(|(i, position)| {
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
                bone_ids: [0; 4],
                bone_weights: [0.0; 4],
            }
        })
        .collect();
    Some((vertices, indices, uvs.is_some()))
}

// Each mesh stored once and drawn at every node that places it
fn static_geometry(
    scene: &Scene,
    uv_channel: usize,
    material_count: usize,
) -> Result<Geometry, Box<dyn Error>> {
    let mut vertices = Vec::<Vertex>::new();
    let mut indices = Vec::<u32>::new();
    let mut batches = Vec::<MeshBatch>::new();
    let mut scene_to_gpu = vec![None; scene.num_meshes()];
    let mut has_uvs = false;

    for (scene_index, gpu_slot) in scene_to_gpu.iter_mut().enumerate() {
        let Some(mesh) = scene.mesh(scene_index) else {
            continue;
        };
        let Some((mesh_vertices, mesh_indices, mesh_has_uvs)) = mesh_vertices(&mesh, uv_channel)
        else {
            continue;
        };
        has_uvs |= mesh_has_uvs;

        let first_index = u32::try_from(indices.len())?;
        let base_vertex = i32::try_from(vertices.len())?;
        vertices.extend(mesh_vertices);
        indices.extend(mesh_indices);

        *gpu_slot = Some(batches.len());
        batches.push(MeshBatch {
            index_range: first_index..u32::try_from(indices.len())?,
            base_vertex,
            instance_range: 0..0,
            material: mesh.material_index().min(material_count - 1),
        });
    }

    let mut instance_batches = vec![Vec::<(Instance, Vec3)>::new(); batches.len()];
    let mut bounds = AABB::empty();
    if let Some(root) = scene.root_node() {
        visit_node(
            scene,
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

    Ok(Geometry {
        vertices,
        indices,
        instances,
        instance_centers,
        batches,
        bounds,
        has_uvs,
        animation: None,
    })
}

// Every mesh at every node that places it, in mesh space, posed by bones.
// Vertices without a bone follow their node, so rigid parts move with node
// animations. None when there are more bones than the shader holds.
fn animated_geometry(
    scene: &Scene,
    uv_channel: usize,
    material_count: usize,
) -> Result<Option<Geometry>, Box<dyn Error>> {
    let Some(root) = scene.root_node() else {
        return Ok(None);
    };
    let mut skeleton = Skeleton::from_scene(root.clone());
    let mut vertices = Vec::<Vertex>::new();
    let mut indices = Vec::<u32>::new();
    let mut batches = Vec::<MeshBatch>::new();
    let mut has_uvs = false;

    let mut stack = vec![root];
    while let Some(node) = stack.pop() {
        for scene_index in node.mesh_indices_iter() {
            let Some(mesh) = scene.mesh(scene_index) else {
                continue;
            };
            let Some((mut mesh_vertices, mesh_indices, mesh_has_uvs)) =
                mesh_vertices(&mesh, uv_channel)
            else {
                continue;
            };
            has_uvs |= mesh_has_uvs;

            for bone in mesh.bones() {
                let index = skeleton.bone_index(&bone.name(), bone.offset_matrix().into());
                for weight in bone.weights_raw() {
                    if let Some(vertex) = mesh_vertices.get_mut(weight.mVertexId as usize) {
                        vertex.add_bone(index, weight.mWeight);
                    }
                }
            }
            if mesh_vertices.iter().any(|v| v.bone_weights == [0.0; 4]) {
                let node_bone = skeleton.bone_index(&node.name(), Mat4::IDENTITY);
                for vertex in &mut mesh_vertices {
                    if vertex.bone_weights == [0.0; 4] {
                        vertex.add_bone(node_bone, 1.0);
                    }
                }
            }
            for vertex in &mut mesh_vertices {
                let total: f32 = vertex.bone_weights.iter().sum();
                vertex.bone_weights = vertex.bone_weights.map(|w| w / total);
            }

            let first_index = u32::try_from(indices.len())?;
            let base_vertex = i32::try_from(vertices.len())?;
            vertices.extend(mesh_vertices);
            indices.extend(mesh_indices);
            batches.push(MeshBatch {
                index_range: first_index..u32::try_from(indices.len())?,
                base_vertex,
                // Bones place the mesh, so each copy has a single fixed instance
                instance_range: batches.len() as u32..batches.len() as u32 + 1,
                material: mesh.material_index().min(material_count - 1),
            });
        }
        stack.extend(node.children());
    }

    if skeleton.bone_count() > MAX_BONES {
        eprintln!(
            "Warning: model uses {} bones but at most {MAX_BONES} are supported; \
             animations are disabled",
            skeleton.bone_count()
        );
        return Ok(None);
    }

    // Bounds and draw-order centers from the rest pose
    let mut rest_pose = Vec::new();
    skeleton.pose(None, &mut rest_pose);
    let mut bounds = AABB::empty();
    let mut instance_centers = Vec::with_capacity(batches.len());
    for batch in &batches {
        let mut batch_bounds = AABB::empty();
        let range = &indices[batch.index_range.start as usize..batch.index_range.end as usize];
        for &index in range {
            let vertex = &vertices[batch.base_vertex as usize + index as usize];
            batch_bounds.expand_to_include_point(vertex.skinned_position(&rest_pose).into());
        }
        bounds.expand_to_include_aabb(&batch_bounds);
        instance_centers.push(Vec3::from(batch_bounds.center()));
    }

    let identity = Instance {
        model: Mat4::IDENTITY.to_cols_array_2d(),
        normal: Mat3::IDENTITY.to_cols_array_2d(),
    };
    let animations = Animation::load_all(scene, &skeleton);
    Ok(Some(Geometry {
        instances: vec![identity; batches.len()],
        instance_centers,
        vertices,
        indices,
        batches,
        bounds,
        has_uvs,
        animation: Some(AnimationData {
            skeleton,
            animations,
        }),
    }))
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

// The file name of a path written on any platform. Windows rules accept both
// \ and / as separators, so they split paths from either platform.
fn file_name(path: &str) -> &str {
    Utf8WindowsPath::new(path).file_name().unwrap_or(path)
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
    let native = Path::new(path);
    if native.is_absolute() && native.exists() {
        return native.to_path_buf();
    }

    // Parsed with Windows rules, which also accept Unix paths
    let written = Utf8WindowsPath::new(path);

    // An absolute path from the machine the model was made on: C:\...,
    // \\server\... or /home/...
    if written.has_root() || written.components().has_prefix() {
        return model_dir.join(file_name(path));
    }

    // A relative path, rebuilt with this platform's separators
    model_dir.join(
        written
            .components()
            .map(|component| component.as_str())
            .collect::<PathBuf>(),
    )
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
    fn skinned_gltf_animates() {
        use glam::Vec3;
        let model = ModelData::load(
            Path::new("tests/fixtures/gltf/skinned.gltf"),
            TextureOverrides::default(),
        )
        .unwrap();
        assert_valid_geometry(&model);
        let data = model.animation.as_ref().expect("model should be animated");
        let names: Vec<&str> = data.animations.iter().map(|a| a.name.as_str()).collect();
        assert_eq!(names, ["bend", "wave"]);
        for vertex in &model.vertices {
            let total: f32 = vertex.bone_weights.iter().sum();
            assert!((total - 1.0).abs() < 1e-5, "{:?}", vertex.bone_weights);
        }

        let pose = |animation: Option<usize>| {
            let mut bones = Vec::new();
            let animation = animation.map(|i| (&data.animations[i], data.animations[i].duration()));
            data.skeleton.pose(animation, &mut bones);
            bones
        };
        let rest = pose(None);
        let vertex_at = |position: Vec3| {
            model
                .vertices
                .iter()
                .find(|v| v.skinned_position(&rest).abs_diff_eq(position, 1e-4))
                .unwrap_or_else(|| panic!("no vertex at {position} in the rest pose"))
        };
        let bar_top = vertex_at(Vec3::new(0.1, 2.0, 0.0));
        let bar_middle = vertex_at(Vec3::new(0.1, 1.0, 0.0));
        let flag_corner = vertex_at(Vec3::new(2.2, 0.2, 0.0));

        // At the end of "bend" the elbow has turned 90 degrees about Z: the
        // top of the bar swings around it, the middle row (half on each bone)
        // lands between, and the rigid flag follows its node up by one
        let bend = pose(Some(0));
        let check = |vertex: &Vertex, bones: &[Mat4], expected: Vec3| {
            let actual = vertex.skinned_position(bones);
            assert!(actual.abs_diff_eq(expected, 1e-4), "{actual} != {expected}");
        };
        check(bar_top, &bend, Vec3::new(-1.0, 1.1, 0.0));
        check(bar_middle, &bend, Vec3::new(0.05, 1.05, 0.0));
        check(flag_corner, &bend, Vec3::new(2.2, 1.2, 0.0));
        // "wave" turns the other way
        check(bar_top, &pose(Some(1)), Vec3::new(1.0, 0.9, 0.0));

        // Framing uses the rest pose, flag included
        let min = Vec3::from(model.bounds.min);
        let max = Vec3::from(model.bounds.max);
        assert!(min.abs_diff_eq(Vec3::new(-0.1, -0.2, 0.0), 1e-4), "{min}");
        assert!(max.abs_diff_eq(Vec3::new(2.2, 2.0, 0.0), 1e-4), "{max}");
    }

    // A real-world rigged FBX (Quaternius, CC0)
    #[test]
    fn rigged_fbx_character_animates() {
        use glam::Vec3;
        let model = ModelData::load(
            Path::new("tests/fixtures/character/character_animated.fbx"),
            TextureOverrides::default(),
        )
        .unwrap();
        assert_valid_geometry(&model);
        let data = model.animation.as_ref().expect("model should be animated");
        let names: Vec<&str> = data.animations.iter().map(|a| a.name.as_str()).collect();
        assert!(names.len() >= 13, "{names:?}");
        for expected in ["Idle", "Walk", "Run"] {
            assert!(names.contains(&expected), "no {expected} in {names:?}");
        }
        for vertex in &model.vertices {
            let total: f32 = vertex.bone_weights.iter().sum();
            assert!((total - 1.0).abs() < 1e-4, "{:?}", vertex.bone_weights);
        }

        // Framed upright: after the up-axis conversion it's taller than wide
        let size = model
            .coordinate_system
            .transform_vector3(Vec3::from(model.bounds.max) - Vec3::from(model.bounds.min))
            .abs();
        assert!(size.y > size.x && size.y > size.z, "{size}");

        // Walking moves the vertices away from the rest pose
        let walk = data.animations.iter().find(|a| a.name == "Walk").unwrap();
        let (mut rest, mut walking) = (Vec::new(), Vec::new());
        data.skeleton.pose(None, &mut rest);
        data.skeleton
            .pose(Some((walk, walk.duration() / 4.0)), &mut walking);
        let height = size.y;
        let moved = model
            .vertices
            .iter()
            .filter(|v| {
                v.skinned_position(&rest)
                    .distance(v.skinned_position(&walking))
                    > height * 0.01
            })
            .count();
        assert!(
            moved > model.vertices.len() / 10,
            "{moved} of {} moved",
            model.vertices.len()
        );
    }

    #[test]
    fn static_models_have_no_animation() {
        assert!(load("spot_triangulated.obj").animation.is_none());
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
        assert_eq!(
            resolve_file_path(dir, r"\\server\share\tex.png"),
            dir.join("tex.png")
        );
        // Relative paths keep their folders in either separator style
        let in_textures = dir.join("textures").join("tex.png");
        assert_eq!(resolve_file_path(dir, r"textures\tex.png"), in_textures);
        assert_eq!(resolve_file_path(dir, "textures/tex.png"), in_textures);
        assert_eq!(
            resolve_file_path(dir, r"..\shared\tex.png"),
            dir.join("..").join("shared").join("tex.png")
        );
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
