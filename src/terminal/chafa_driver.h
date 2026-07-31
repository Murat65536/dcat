#pragma once

#include "core/args.h"
#include <chafa.h>
#include <stdbool.h>
#include <stdint.h>

void chafa_driver_configure(const Args *args);
ChafaTermInfo *chafa_driver_get_term_info(void);
bool chafa_driver_uses_character_cells(void);
void chafa_driver_render(const uint8_t *framebuffer, uint32_t width, uint32_t height);
void chafa_driver_cleanup(void);
