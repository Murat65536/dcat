use image::GenericImageView;
use std::path::Path;

pub struct Texture {
    pub width: u32,
    pub height: u32,
    pub data: Vec<u8>,
}

impl Texture {
    pub fn new(width: u32, height: u32) -> Self {
        let size = (width * height * 3) as usize;
        Texture {
            width,
            height,
            data: vec![127; size],
        }
    }

    pub fn from_image(path: &Path) -> Result<Self, image::ImageError> {
        let img = image::open(path)?;
        let (width, height) = img.dimensions();
        let rgb_img = img.to_rgb8();

        let data: Vec<u8> = rgb_img.into_raw();

        Ok(Texture {
            width,
            height,
            data,
        })
    }

    pub fn create_flat_normal_map() -> Self {
        let mut texture = Texture {
            width: 1,
            height: 1,
            data: vec![0; 3],
        };
        texture.data[0] = 128;
        texture.data[1] = 128;
        texture.data[2] = 255;
        texture
    }
}
