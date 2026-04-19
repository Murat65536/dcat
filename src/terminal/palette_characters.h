#ifndef DCAT_PALETTE_CHARACTERS_H_
#define DCAT_PALETTE_CHARACTERS_H_

#include <stdbool.h>
#include <stdint.h>

void render_palette_characters(const uint8_t* buffer, uint32_t width, uint32_t height,
                               bool use_hash_characters);

#endif
