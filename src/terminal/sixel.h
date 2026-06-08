#pragma once
#include <stdbool.h>
#include <stdint.h>

bool detect_sixel_support(void);
void render_sixel(const uint8_t *buffer, uint32_t width, uint32_t height, bool use_hash_characters);
