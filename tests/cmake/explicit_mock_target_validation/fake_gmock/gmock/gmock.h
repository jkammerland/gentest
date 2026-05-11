#pragma once

#define GENTEST_FAKE_GMOCK_UNPAREN(...) (__VA_ARGS__)
#define MOCK_METHOD(Return, Name, Args, Specs)                                                                                             \
    Return Name GENTEST_FAKE_GMOCK_UNPAREN Args { return Return{}; }
