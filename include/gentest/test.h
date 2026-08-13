#pragma once

// Recommended include for ordinary synchronous Gentest cases. It provides
// attributes, assertions, logging, and skip/xfail support without importing
// the coroutine API or runner registry surface. Include gentest/runner.h for
// async tests, fixtures, manual registration, or runner entry points.

#include "gentest/assertions.h"
#include "gentest/attributes.h"
#include "gentest/context.h"
