#ifndef DCAT_TRUECOLOR_CHARACTERS_H_
#define DCAT_TRUECOLOR_CHARACTERS_H_

#include <stdbool.h>
#include <stdint.h>

void render_truecolor_characters(const uint8_t* buffer, uint32_t width, uint32_t height);
bool detect_truecolor_support(void);

#endif
