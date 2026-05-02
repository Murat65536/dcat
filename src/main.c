#include "core/app.h"

int main(const int argc, char *argv[]) {
    AppContext *app = app_create();
    if (!app) {
        return 1;
    }

    int exit_code = 1;
    if (app_init(app, argc, argv)) {
        exit_code = app_run_loop(app);
    }

    app_cleanup(app);
    app_destroy(app);
    return exit_code;
}
