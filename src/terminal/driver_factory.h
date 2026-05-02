#ifndef DCAT_DRIVER_FACTORY_H
#define DCAT_DRIVER_FACTORY_H

#include "core/args.h"
#include "terminal/output_driver.h"

const OutputDriver* driver_factory_get(const Args *args);
bool driver_is_supported_on_platform(const OutputDriver *driver);

#endif // DCAT_DRIVER_FACTORY_H
