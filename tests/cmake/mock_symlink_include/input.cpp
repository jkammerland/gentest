#include "../include/symlink_sink.hpp"
#include "gentest/mock.h"
using SinkMock                                 = gentest::mock<symlinkprobe::Sink>;
[[maybe_unused]] inline SinkMock *kSinkMockPtr = nullptr;
