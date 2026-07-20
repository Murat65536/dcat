#pragma once
#include "core/args.h"
#include "terminal/output_driver.h"

const OutputDriver *driver_factory_get(const Args *args);
