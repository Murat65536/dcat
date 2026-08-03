use image::DynamicImage;
use ratatui::crossterm::event;
use ratatui_image::{Image, Resize, picker::Picker};

pub fn draw(image: DynamicImage) {
    ratatui::run(|terminal| {
        let picker = Picker::from_query_stdio().expect("Expected to create picker");
        println!("{:?}", picker.protocol_type());
        let protocol = picker
            .new_protocol(image, terminal.size().unwrap(), Resize::Scale(None))
            .expect("Expected to create protocol");
        let _ = terminal.draw(|frame| {
            let image = Image::new(&protocol);
            frame.render_widget(image, frame.area());
        });
        while !event::read().expect("Expected read").is_key_press() {}
    });
}
