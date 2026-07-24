#pragma once

#include <chafa.h>
#include <stdbool.h>
#include <stdint.h>

void chafa_driver_detect(ChafaPixelMode *pixel_mode, ChafaCanvasMode *canvas_mode);
ChafaPixelMode chafa_driver_pixel_mode_from_response(const char *response);
ChafaDitherMode chafa_driver_dither_mode(ChafaPixelMode pixel_mode, ChafaCanvasMode canvas_mode);
void chafa_driver_configure(ChafaPixelMode pixel_mode, ChafaCanvasMode canvas_mode);
void chafa_driver_render(const uint8_t *framebuffer, uint32_t width, uint32_t height,
                         bool use_hash_characters);
void chafa_driver_cleanup(void);
