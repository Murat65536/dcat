use std::path::Path;

use image::{RgbaImage, imageops::FilterType};

// Decoded RGBA8 image ready for upload
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TextureImage {
    pub width: u32,
    pub height: u32,
    pub pixels: Vec<u8>,
}

impl TextureImage {
    pub fn solid(rgba: [u8; 4]) -> Self {
        Self {
            width: 1,
            height: 1,
            pixels: rgba.to_vec(),
        }
    }

    // Used when a material has no diffuse texture or it fails to load
    pub fn default_diffuse() -> Self {
        Self::solid([127, 127, 127, 255])
    }

    // Normal map that leaves surface normals unchanged
    pub fn flat_normal() -> Self {
        Self::solid([127, 127, 255, 255])
    }

    pub fn from_file(path: &Path) -> Result<Self, image::ImageError> {
        Ok(Self::from_rgba(image::open(path)?.into_rgba8()))
    }

    pub fn from_memory(bytes: &[u8]) -> Result<Self, image::ImageError> {
        Ok(Self::from_rgba(
            image::load_from_memory(bytes)?.into_rgba8(),
        ))
    }

    pub fn from_rgba(image: RgbaImage) -> Self {
        Self {
            width: image.width(),
            height: image.height(),
            pixels: image.into_raw(),
        }
    }

    pub fn has_transparency(&self) -> bool {
        self.pixels
            .as_chunks::<4>()
            .0
            .iter()
            .any(|pixel| pixel[3] < 255)
    }

    // Shrink to fit within max_dimension on both axes, keeping the aspect ratio
    pub fn fit_within(self, max_dimension: u32) -> Self {
        if self.width <= max_dimension && self.height <= max_dimension {
            return self;
        }

        let scale = max_dimension as f32 / self.width.max(self.height) as f32;
        let width = ((self.width as f32 * scale) as u32).clamp(1, max_dimension);
        let height = ((self.height as f32 * scale) as u32).clamp(1, max_dimension);
        let image = RgbaImage::from_raw(self.width, self.height, self.pixels)
            .expect("pixel buffer matches dimensions");
        Self::from_rgba(image::imageops::resize(
            &image,
            width,
            height,
            FilterType::Triangle,
        ))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn transparency_detection() {
        assert!(!TextureImage::default_diffuse().has_transparency());
        assert!(TextureImage::solid([0, 0, 0, 254]).has_transparency());
    }

    #[test]
    fn loads_fixture_texture() {
        let texture =
            TextureImage::from_file(Path::new("tests/fixtures/spot/spot_texture.png")).unwrap();
        assert!(texture.width > 0 && texture.height > 0);
        assert_eq!(
            texture.pixels.len(),
            (texture.width * texture.height * 4) as usize
        );
    }

    #[test]
    fn fit_within_preserves_aspect() {
        let texture = TextureImage::from_rgba(RgbaImage::new(400, 100)).fit_within(200);
        assert_eq!((texture.width, texture.height), (200, 50));
        assert_eq!(texture.pixels.len(), 200 * 50 * 4);

        let small = TextureImage::default_diffuse().fit_within(200);
        assert_eq!(small, TextureImage::default_diffuse());
    }

    #[test]
    fn missing_file_is_an_error() {
        assert!(TextureImage::from_file(Path::new("does/not/exist.png")).is_err());
    }
}
