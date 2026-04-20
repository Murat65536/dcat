#ifndef DCAT_BLOCK_CHARACTERS_H_
#define DCAT_BLOCK_CHARACTERS_H_

#include <stdbool.h>
#include <stdint.h>

void render_block_characters(const uint8_t *buffer, uint32_t width, uint32_t height,
                             bool use_hash_characters);

#endif
