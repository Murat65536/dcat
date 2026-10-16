use std::{
    collections::HashMap,
    time::{Duration, Instant},
};

use crossterm::event::{KeyCode, ModifierKeyCode};

// Without release events a press counts as held for this long. It covers the
// usual delay before key autorepeat starts, so holding a key moves smoothly.
const FALLBACK_HOLD: Duration = Duration::from_millis(600);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum HeldKey {
    Forward,
    Back,
    Left,
    Right,
    Up,
    Down,
    LookUp,
    LookDown,
    LookLeft,
    LookRight,
    Slower,
    Faster,
    Precise,
}

impl HeldKey {
    pub fn from_code(code: KeyCode) -> Option<Self> {
        let key = match code {
            KeyCode::Char(c) => match c.to_ascii_lowercase() {
                'w' => Self::Forward,
                's' => Self::Back,
                'a' => Self::Left,
                'd' => Self::Right,
                ' ' => Self::Up,
                'i' => Self::LookUp,
                'k' => Self::LookDown,
                'j' => Self::LookLeft,
                'l' => Self::LookRight,
                'v' => Self::Slower,
                'b' => Self::Faster,
                _ => return None,
            },
            KeyCode::Modifier(ModifierKeyCode::LeftShift | ModifierKeyCode::RightShift) => {
                Self::Down
            }
            KeyCode::Modifier(ModifierKeyCode::LeftControl | ModifierKeyCode::RightControl) => {
                Self::Precise
            }
            _ => return None,
        };
        Some(key)
    }
}

// Keys currently held down, for continuous first-person movement
pub struct HeldKeys {
    pressed_at: HashMap<HeldKey, Instant>,
    reports_releases: bool,
}

impl HeldKeys {
    pub fn new(reports_releases: bool) -> Self {
        Self {
            pressed_at: HashMap::new(),
            reports_releases,
        }
    }

    pub fn press(&mut self, key: HeldKey, now: Instant) {
        self.pressed_at.insert(key, now);
    }

    pub fn release(&mut self, key: HeldKey) {
        self.pressed_at.remove(&key);
    }

    pub fn is_held(&self, key: HeldKey, now: Instant) -> bool {
        self.pressed_at.get(&key).is_some_and(|&pressed_at| {
            self.reports_releases || now.duration_since(pressed_at) < FALLBACK_HOLD
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn key_mapping_ignores_case() {
        assert_eq!(
            HeldKey::from_code(KeyCode::Char('w')),
            Some(HeldKey::Forward)
        );
        assert_eq!(
            HeldKey::from_code(KeyCode::Char('W')),
            Some(HeldKey::Forward)
        );
        assert_eq!(HeldKey::from_code(KeyCode::Char(' ')), Some(HeldKey::Up));
        assert_eq!(
            HeldKey::from_code(KeyCode::Modifier(ModifierKeyCode::RightShift)),
            Some(HeldKey::Down)
        );
        assert_eq!(HeldKey::from_code(KeyCode::Char('x')), None);
    }

    #[test]
    fn held_until_released() {
        let start = Instant::now();
        let mut keys = HeldKeys::new(true);
        keys.press(HeldKey::Forward, start);
        assert!(keys.is_held(HeldKey::Forward, start + Duration::from_secs(10)));
        keys.release(HeldKey::Forward);
        assert!(!keys.is_held(HeldKey::Forward, start));
    }

    #[test]
    fn fallback_presses_expire() {
        let start = Instant::now();
        let mut keys = HeldKeys::new(false);
        keys.press(HeldKey::Forward, start);
        assert!(keys.is_held(HeldKey::Forward, start + FALLBACK_HOLD / 2));
        assert!(!keys.is_held(HeldKey::Forward, start + FALLBACK_HOLD));

        // Autorepeat refreshes the hold
        keys.press(HeldKey::Forward, start + FALLBACK_HOLD / 2);
        assert!(keys.is_held(HeldKey::Forward, start + FALLBACK_HOLD));
    }
}
