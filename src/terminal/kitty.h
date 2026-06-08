#pragma once
#include <stdbool.h>
#include <stdint.h>

void render_kitty(const uint8_t *buffer, uint32_t width, uint32_t height, bool use_hash_characters);
bool detect_kitty_support(void);
