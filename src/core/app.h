#pragma once
#include <stdbool.h>

#include "args.h"

typedef struct AppContext AppContext;

AppContext *app_create(void);
void app_destroy(AppContext *app);
// args must already be parsed and validated; prog_name is argv[0] (used to
// initialize libvips). Takes a copy of *args.
bool app_init(AppContext *app, const Args *args, const char *prog_name);
int app_run_loop(AppContext *app);
void app_cleanup(AppContext *app);
