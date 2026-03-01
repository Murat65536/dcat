#ifndef DCAT_KITTY_H_
#define DCAT_KITTY_H_

#include <stdbool.h>
#include <stdint.h>

void render_kitty(const uint8_t* buffer, uint32_t width, uint32_t height);
bool detect_kitty_support(void);

#endif
