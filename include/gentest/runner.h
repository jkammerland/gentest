#pragma once

// Umbrella include for existing consumers. New code can include narrower
// surfaces directly:
// - gentest/assertions.h
// - gentest/context.h
// - gentest/format_value.h
// - gentest/registry.h
//
// Runner entry points, including run_cases(), are declared by the narrow
// registry API included below.

#include "gentest/assertions.h"
#include "gentest/async.h"
#include "gentest/context.h"
#include "gentest/detail/fixture_api.h"
#include "gentest/detail/registry_api.h"
#include "gentest/format_value.h"
