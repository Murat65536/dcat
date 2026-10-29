use std::collections::HashMap;

use asset_importer::{Scene, node::Node};
use glam::{Mat4, Quat, Vec3};

// Bone matrices the shader holds: 1024 4x4 matrices fill a 64 KiB uniform buffer
pub const MAX_BONES: usize = 1024;
// Assumed when a file doesn't give a rate
const DEFAULT_TICKS_PER_SECOND: f32 = 25.0;

// A node of the scene hierarchy with its rest pose
struct SkeletonNode {
    parent: Option<usize>,
    rest: Mat4,
    // The rest pose split up, for animations that only key some components
    translation: Vec3,
    rotation: Quat,
    scale: Vec3,
}

struct Bone {
    // The node that moves the bone, if the hierarchy has one by its name
    node: Option<usize>,
    // From mesh space into the bone's space at bind time
    offset: Mat4,
}

// The scene's node hierarchy and the bones that hang off it. Nodes are stored
// parents first, so global transforms come out of a single pass.
pub struct Skeleton {
    nodes: Vec<SkeletonNode>,
    node_by_name: HashMap<String, usize>,
    bones: Vec<Bone>,
    bone_by_name: HashMap<String, usize>,
}

impl Skeleton {
    pub fn from_scene(root: Node) -> Self {
        let mut skeleton = Self {
            nodes: Vec::new(),
            node_by_name: HashMap::new(),
            bones: Vec::new(),
            bone_by_name: HashMap::new(),
        };
        let mut stack = vec![(root, None)];
        while let Some((node, parent)) = stack.pop() {
            let index = skeleton.nodes.len();
            let rest: Mat4 = node.transformation().into();
            let (scale, rotation, translation) = rest.to_scale_rotation_translation();
            skeleton.nodes.push(SkeletonNode {
                parent,
                rest,
                translation,
                rotation,
                scale,
            });
            // Names aren't guaranteed unique; the first node with a name wins
            skeleton.node_by_name.entry(node.name()).or_insert(index);

            let children: Vec<Node> = node.children().collect();
            stack.extend(children.into_iter().rev().map(|child| (child, Some(index))));
        }
        skeleton
    }

    // The bone with this name, added with the given offset if it's new
    pub fn bone_index(&mut self, name: &str, offset: Mat4) -> usize {
        if let Some(&index) = self.bone_by_name.get(name) {
            return index;
        }
        let index = self.bones.len();
        self.bones.push(Bone {
            node: self.node_by_name.get(name).copied(),
            offset,
        });
        self.bone_by_name.insert(name.to_owned(), index);
        index
    }

    pub fn bone_count(&self) -> usize {
        self.bones.len()
    }

    fn node_index(&self, name: &str) -> Option<usize> {
        self.node_by_name.get(name).copied()
    }

    // Bone matrices at a time (in ticks) of an animation, or the rest pose
    pub fn pose(&self, animation: Option<(&Animation, f32)>, bones: &mut Vec<Mat4>) {
        let mut globals: Vec<Mat4> = Vec::with_capacity(self.nodes.len());
        for (index, node) in self.nodes.iter().enumerate() {
            let local = animation
                .and_then(|(animation, time)| {
                    let channel = animation.channels.get(index)?.as_ref()?;
                    Some(channel.sample(time, node))
                })
                .unwrap_or(node.rest);
            let global = node.parent.map_or(local, |parent| globals[parent] * local);
            globals.push(global);
        }

        bones.clear();
        bones.extend(self.bones.iter().map(|bone| {
            // A bone missing from the hierarchy stays where it was bound
            bone.node
                .map_or(Mat4::IDENTITY, |node| globals[node] * bone.offset)
        }));
    }
}

struct Key<T> {
    time: f32,
    value: T,
}

// Keyframes for one node; empty lists keep the node's rest value
struct Channel {
    positions: Vec<Key<Vec3>>,
    rotations: Vec<Key<Quat>>,
    scales: Vec<Key<Vec3>>,
}

impl Channel {
    fn sample(&self, time: f32, node: &SkeletonNode) -> Mat4 {
        let translation =
            interpolate(&self.positions, time, Vec3::lerp).unwrap_or(node.translation);
        let rotation = interpolate(&self.rotations, time, slerp_shortest).unwrap_or(node.rotation);
        let scale = interpolate(&self.scales, time, Vec3::lerp).unwrap_or(node.scale);
        Mat4::from_scale_rotation_translation(scale, rotation, translation)
    }
}

// The value between the keys around `time`, holding the first and last keys
// outside their range. None without keys.
fn interpolate<T: Copy>(keys: &[Key<T>], time: f32, mix: fn(T, T, f32) -> T) -> Option<T> {
    let next = keys.partition_point(|key| key.time <= time);
    match (next.checked_sub(1).map(|i| &keys[i]), keys.get(next)) {
        (Some(before), Some(after)) => {
            let span = after.time - before.time;
            let factor = if span > 1e-5 {
                ((time - before.time) / span).clamp(0.0, 1.0)
            } else {
                0.0
            };
            Some(mix(before.value, after.value, factor))
        }
        (Some(only), None) | (None, Some(only)) => Some(only.value),
        (None, None) => None,
    }
}

// Rotate the short way round between two orientations
fn slerp_shortest(from: Quat, to: Quat, factor: f32) -> Quat {
    let to = if from.dot(to) < 0.0 { -to } else { to };
    from.slerp(to, factor).normalize()
}

pub struct Animation {
    pub name: String,
    // In ticks
    duration: f32,
    ticks_per_second: f32,
    // The channel moving each skeleton node, if any
    channels: Vec<Option<Channel>>,
}

impl Animation {
    // Length in ticks
    pub fn duration(&self) -> f32 {
        self.duration
    }

    pub fn load_all(scene: &Scene, skeleton: &Skeleton) -> Vec<Self> {
        scene
            .animations()
            .map(|source| {
                let mut channels: Vec<Option<Channel>> =
                    (0..skeleton.nodes.len()).map(|_| None).collect();
                for channel in source.channels() {
                    let Some(node) = skeleton.node_index(&channel.node_name()) else {
                        continue;
                    };
                    channels[node] = Some(Channel {
                        positions: channel
                            .position_keys_iter()
                            .map(|key| Key {
                                time: key.time as f32,
                                value: key.value.into(),
                            })
                            .collect(),
                        rotations: channel
                            .rotation_keys_iter()
                            .map(|key| Key {
                                time: key.time as f32,
                                value: Quat::from(key.value).normalize(),
                            })
                            .collect(),
                        scales: channel
                            .scaling_keys_iter()
                            .map(|key| Key {
                                time: key.time as f32,
                                value: key.value.into(),
                            })
                            .collect(),
                    });
                }

                // Some exporters declare a longer duration than the keys
                // cover, which would leave the model still at the end of each loop
                let last_key = channels
                    .iter()
                    .flatten()
                    .flat_map(|c| {
                        [
                            c.positions.last().map(|k| k.time),
                            c.rotations.last().map(|k| k.time),
                            c.scales.last().map(|k| k.time),
                        ]
                    })
                    .flatten()
                    .fold(0.0f32, f32::max);
                let declared = source.duration() as f32;
                let duration = if last_key > 0.0 && last_key < declared {
                    last_key
                } else {
                    declared
                };

                let ticks_per_second = match source.ticks_per_second() as f32 {
                    rate if rate > 0.0 => rate,
                    _ => DEFAULT_TICKS_PER_SECOND,
                };

                Animation {
                    name: source.name(),
                    duration,
                    ticks_per_second,
                    channels,
                }
            })
            .collect()
    }
}

// Which animation plays and where in it
pub struct AnimationPlayer {
    pub current: usize,
    // In ticks of the current animation
    time: f32,
    pub playing: bool,
}

impl Default for AnimationPlayer {
    fn default() -> Self {
        Self::new()
    }
}

impl AnimationPlayer {
    pub fn new() -> Self {
        Self {
            current: 0,
            time: 0.0,
            playing: true,
        }
    }

    pub fn time(&self) -> f32 {
        self.time
    }

    pub fn advance(&mut self, animation: &Animation, delta_seconds: f32) {
        if !self.playing {
            return;
        }
        self.time += delta_seconds * animation.ticks_per_second;
        if animation.duration > 0.0 && self.time >= animation.duration {
            self.time %= animation.duration;
        }
    }

    pub fn toggle_playing(&mut self) {
        self.playing = !self.playing;
    }

    // Switch to the next or previous of `count` animations, from the start
    pub fn step(&mut self, forward: bool, count: usize) {
        if count == 0 {
            return;
        }
        self.current = if forward {
            (self.current + 1) % count
        } else {
            (self.current + count - 1) % count
        };
        self.time = 0.0;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f32::consts::FRAC_PI_2;

    fn keys<T: Copy>(values: &[(f32, T)]) -> Vec<Key<T>> {
        values
            .iter()
            .map(|&(time, value)| Key { time, value })
            .collect()
    }

    #[test]
    fn interpolation_between_and_outside_keys() {
        let positions = keys(&[(1.0, Vec3::ZERO), (3.0, Vec3::new(4.0, 0.0, 0.0))]);
        let at = |t| interpolate(&positions, t, Vec3::lerp).unwrap();
        assert_eq!(at(2.0), Vec3::new(2.0, 0.0, 0.0));
        assert_eq!(at(0.0), Vec3::ZERO);
        assert_eq!(at(9.0), Vec3::new(4.0, 0.0, 0.0));
        assert_eq!(interpolate::<Vec3>(&[], 1.0, Vec3::lerp), None);
        assert_eq!(
            interpolate(&keys(&[(5.0, Vec3::ONE)]), 0.0, Vec3::lerp),
            Some(Vec3::ONE)
        );
    }

    #[test]
    fn rotation_takes_the_short_way() {
        let start = Quat::from_rotation_y(0.0);
        // The same 90 degree turn written with a negated quaternion
        let end = -Quat::from_rotation_y(FRAC_PI_2);
        let halfway = slerp_shortest(start, end, 0.5);
        let expected = Quat::from_rotation_y(FRAC_PI_2 / 2.0);
        assert!(halfway.dot(expected).abs() > 0.9999, "{halfway}");
    }

    fn two_bone_skeleton() -> Skeleton {
        // root -> arm (one unit up) -> hand (one unit further up)
        let node = |parent, y: f32| SkeletonNode {
            parent,
            rest: Mat4::from_translation(Vec3::new(0.0, y, 0.0)),
            translation: Vec3::new(0.0, y, 0.0),
            rotation: Quat::IDENTITY,
            scale: Vec3::ONE,
        };
        let mut skeleton = Skeleton {
            nodes: vec![node(None, 0.0), node(Some(0), 1.0), node(Some(1), 1.0)],
            node_by_name: [("root", 0), ("arm", 1), ("hand", 2)]
                .map(|(name, index)| (name.to_owned(), index))
                .into(),
            bones: Vec::new(),
            bone_by_name: HashMap::new(),
        };
        // Offsets undo each bone's bind position
        skeleton.bone_index("arm", Mat4::from_translation(Vec3::new(0.0, -1.0, 0.0)));
        skeleton.bone_index("hand", Mat4::from_translation(Vec3::new(0.0, -2.0, 0.0)));
        skeleton
    }

    #[test]
    fn rest_pose_leaves_vertices_in_place() {
        let skeleton = two_bone_skeleton();
        let mut bones = Vec::new();
        skeleton.pose(None, &mut bones);
        for bone in &bones {
            assert!(bone.abs_diff_eq(Mat4::IDENTITY, 1e-6), "{bone}");
        }
    }

    #[test]
    fn rotating_a_parent_carries_its_children() {
        let skeleton = two_bone_skeleton();
        // Bend the arm 90 degrees about Z over a 2 tick animation
        let mut channels: Vec<Option<Channel>> = (0..3).map(|_| None).collect();
        channels[1] = Some(Channel {
            positions: Vec::new(),
            rotations: keys(&[
                (0.0, Quat::IDENTITY),
                (2.0, Quat::from_rotation_z(FRAC_PI_2)),
            ]),
            scales: Vec::new(),
        });
        let animation = Animation {
            name: "bend".into(),
            duration: 2.0,
            ticks_per_second: 1.0,
            channels,
        };

        // A vertex at the hand's bind position swings around the arm's base
        let hand_tip_at = |time| {
            let mut bones = Vec::new();
            skeleton.pose(Some((&animation, time)), &mut bones);
            bones[1].transform_point3(Vec3::new(0.0, 2.0, 0.0))
        };
        let end = hand_tip_at(2.0);
        assert!(end.abs_diff_eq(Vec3::new(-1.0, 1.0, 0.0), 1e-5), "{end}");
        let halfway = hand_tip_at(1.0);
        let diagonal = std::f32::consts::FRAC_1_SQRT_2;
        assert!(
            halfway.abs_diff_eq(Vec3::new(-diagonal, 1.0 + diagonal, 0.0), 1e-5),
            "{halfway}"
        );

        let mut bones = Vec::new();
        skeleton.pose(Some((&animation, 2.0)), &mut bones);
        // Missing position and scale keys keep the rest values
        let elbow = bones[0].transform_point3(Vec3::new(0.0, 1.0, 0.0));
        assert!(elbow.abs_diff_eq(Vec3::new(0.0, 1.0, 0.0), 1e-5), "{elbow}");
    }

    #[test]
    fn unknown_bones_stay_put() {
        let mut skeleton = two_bone_skeleton();
        let ghost = skeleton.bone_index("ghost", Mat4::from_translation(Vec3::X));
        let mut bones = Vec::new();
        skeleton.pose(None, &mut bones);
        assert_eq!(bones[ghost], Mat4::IDENTITY);
        // Asking again returns the same bone
        assert_eq!(skeleton.bone_index("ghost", Mat4::IDENTITY), ghost);
    }

    #[test]
    fn player_loops_pauses_and_switches() {
        let animation = Animation {
            name: "loop".into(),
            duration: 10.0,
            ticks_per_second: 5.0,
            channels: Vec::new(),
        };
        let mut player = AnimationPlayer::new();
        player.advance(&animation, 1.5);
        assert!((player.time() - 7.5).abs() < 1e-5);
        player.advance(&animation, 1.0);
        assert!((player.time() - 2.5).abs() < 1e-5, "{}", player.time());

        player.toggle_playing();
        player.advance(&animation, 1.0);
        assert!((player.time() - 2.5).abs() < 1e-5);

        player.step(false, 3);
        assert_eq!((player.current, player.time()), (2, 0.0));
        player.step(true, 3);
        assert_eq!(player.current, 0);
    }
}
