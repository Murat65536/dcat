use std::{
    collections::HashMap,
    time::{Duration, Instant},
};

use crossterm::event::{KeyCode, ModifierKeyCode};
use input_query::{InputHandler, Snapshot};

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

impl HeldKey {
    // The keys on the keyboard itself that hold this
    fn device_keys(self) -> &'static [input_query::KeyCode] {
        use input_query::KeyCode as Key;
        match self {
            Self::Forward => &[Key::KeyW],
            Self::Back => &[Key::KeyS],
            Self::Left => &[Key::KeyA],
            Self::Right => &[Key::KeyD],
            Self::Up => &[Key::KeySpace],
            Self::Down => &[Key::KeyLeftShift, Key::KeyRightShift],
            Self::LookUp => &[Key::KeyI],
            Self::LookDown => &[Key::KeyK],
            Self::LookLeft => &[Key::KeyJ],
            Self::LookRight => &[Key::KeyL],
            Self::Slower => &[Key::KeyV],
            Self::Faster => &[Key::KeyB],
            Self::Precise => &[Key::KeyLeftCtrl, Key::KeyRightCtrl],
        }
    }
}

// Keys currently held down, for continuous first-person movement
pub struct HeldKeys {
    pressed_at: HashMap<HeldKey, Instant>,
    reports_releases: bool,
    // The keyboard itself, read directly in place of the terminal's presses
    // when it doesn't report releases
    keyboard: Option<InputHandler>,
    // The keyboard's keys this frame
    keyboard_keys: Option<Snapshot>,
}

impl HeldKeys {
    pub fn new(reports_releases: bool, mut keyboard: Option<InputHandler>) -> Self {
        // The keyboard sees keys whichever window has focus, so they only
        // count once the terminal shows it has focus
        if let Some(keyboard) = &mut keyboard {
            keyboard.set_focused(false);
        }
        Self {
            pressed_at: HashMap::new(),
            reports_releases,
            keyboard,
            keyboard_keys: None,
        }
    }

    pub fn set_focused(&mut self, focused: bool) {
        if let Some(keyboard) = &mut self.keyboard {
            keyboard.set_focused(focused);
        }
    }

    // Called once a frame, before checking keys
    pub fn update(&mut self) {
        self.keyboard_keys = self.keyboard.as_ref().map(InputHandler::snapshot);
    }

    pub fn press(&mut self, key: HeldKey, now: Instant) {
        self.pressed_at.insert(key, now);
    }

    pub fn release(&mut self, key: HeldKey) {
        self.pressed_at.remove(&key);
    }

    pub fn is_held(&self, key: HeldKey, now: Instant) -> bool {
        if let Some(keys) = &self.keyboard_keys {
            return key.device_keys().iter().any(|&code| keys.is_pressed(code));
        }
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
        let mut keys = HeldKeys::new(true, None);
        keys.press(HeldKey::Forward, start);
        assert!(keys.is_held(HeldKey::Forward, start + Duration::from_secs(10)));
        keys.release(HeldKey::Forward);
        assert!(!keys.is_held(HeldKey::Forward, start));
    }

    #[test]
    fn fallback_presses_expire() {
        let start = Instant::now();
        let mut keys = HeldKeys::new(false, None);
        keys.press(HeldKey::Forward, start);
        assert!(keys.is_held(HeldKey::Forward, start + FALLBACK_HOLD / 2));
        assert!(!keys.is_held(HeldKey::Forward, start + FALLBACK_HOLD));

        // Autorepeat refreshes the hold
        keys.press(HeldKey::Forward, start + FALLBACK_HOLD / 2);
        assert!(keys.is_held(HeldKey::Forward, start + FALLBACK_HOLD));
    }

    #[test]
    fn keyboard_keys_replace_terminal_presses() {
        // Needs a readable keyboard, which CI runners don't have
        let Ok(keyboard) = InputHandler::new() else {
            return;
        };
        let now = Instant::now();
        let mut keys = HeldKeys::new(false, Some(keyboard));
        // Until the terminal shows it has focus, the keyboard counts for nothing
        assert!(!keys.keyboard.as_ref().unwrap().is_focused());
        keys.press(HeldKey::Forward, now);
        keys.update();
        assert!(!keys.is_held(HeldKey::Forward, now));

        keys.set_focused(true);
        assert!(keys.keyboard.as_ref().unwrap().is_focused());
    }
}
