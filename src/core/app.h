#ifndef DCAT_APP_H
#define DCAT_APP_H

#include <stdbool.h>

typedef struct AppContext AppContext;

AppContext* app_create(void);
void app_destroy(AppContext *app);
bool app_init(AppContext *app, int argc, char *argv[]);
int app_run_loop(AppContext *app);
void app_cleanup(AppContext *app);

#endif // DCAT_APP_H
