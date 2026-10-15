#pragma once

#ifndef GENTEST_RUNTIME_API
#if defined(GENTEST_RUNTIME_SHARED)
#if defined(_WIN32)
#if defined(GENTEST_RUNTIME_BUILDING)
#define GENTEST_RUNTIME_API __declspec(dllexport)
#else
#define GENTEST_RUNTIME_API __declspec(dllimport)
#endif
#else
#define GENTEST_RUNTIME_API __attribute__((visibility("default")))
#endif
#else
#define GENTEST_RUNTIME_API
#endif
#endif
