#ifndef DCAT_SIXEL_H_
#define DCAT_SIXEL_H_

#include <stdint.h>
#include <stdbool.h>

bool detect_sixel_support(void);
void render_sixel(const uint8_t* buffer, uint32_t width, uint32_t height);

#endif
