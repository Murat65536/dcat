#include "core/app.h"
#include "core/args.h"

int main(const int argc, char *argv[]) {
    Args args = {0};
    switch (parse_args(argc, argv, &args)) {
    case ARGS_PARSE_HELP:
        print_usage();
        return 0;
    case ARGS_PARSE_CONTROLS:
        print_controls();
        return 0;
    case ARGS_PARSE_ERROR:
        return 1;
    case ARGS_PARSE_OK:
        break;
    }

    if (!validate_args(&args)) {
        return 1;
    }

    AppContext *app = app_create();
    if (!app) {
        return 1;
    }

    int exit_code = 1;
    if (app_init(app, &args, argv[0])) {
        exit_code = app_run_loop(app);
    }

    app_cleanup(app);
    app_destroy(app);
    return exit_code;
}
