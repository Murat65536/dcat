#pragma once

#ifdef TRACY_ENABLE
#include <tracy/TracyC.h>
#define DCAT_PROFILE_START() ___tracy_startup_profiler()
#define DCAT_PROFILE_STOP() ___tracy_shutdown_profiler()
#define DCAT_PROFILE_ZONE(context, name) TracyCZoneN(context, name, TracyCIsStarted)
#define DCAT_PROFILE_ZONE_END(context) TracyCZoneEnd(context)
#define DCAT_PROFILE_FRAME_MARK()                                                                  \
    do {                                                                                           \
        if (TracyCIsStarted)                                                                       \
            TracyCFrameMark;                                                                       \
    } while (0)
#define DCAT_PROFILE_PLOT_INT(name, value)                                                         \
    do {                                                                                           \
        if (TracyCIsStarted)                                                                       \
            TracyCPlotI(name, (int64_t)(value));                                                   \
    } while (0)
#define DCAT_PROFILE_PLOT_FLOAT(name, value)                                                       \
    do {                                                                                           \
        if (TracyCIsStarted)                                                                       \
            TracyCPlotF(name, (float)(value));                                                     \
    } while (0)
#else
#define DCAT_PROFILE_START()
#define DCAT_PROFILE_STOP()
#define DCAT_PROFILE_ZONE(context, name)
#define DCAT_PROFILE_ZONE_END(context)
#define DCAT_PROFILE_FRAME_MARK()
#define DCAT_PROFILE_PLOT_INT(name, value)
#define DCAT_PROFILE_PLOT_FLOAT(name, value)
#endif
