#ifndef DCAT_KITTY_SHM_H_
#define DCAT_KITTY_SHM_H_

#include <stdbool.h>
#include <stdint.h>

void render_kitty_shm(const uint8_t *buffer, uint32_t width, uint32_t height);
bool detect_kitty_shm_support(void);

#endif
