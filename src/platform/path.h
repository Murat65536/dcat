#pragma once
#include <stdbool.h>
#include <stddef.h>

bool dcat_get_executable_path(char *out, size_t out_size);
bool dcat_get_executable_directory(char *out, size_t out_size);
