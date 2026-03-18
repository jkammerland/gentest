#include "gentest/mock.h"
#include "../include/symlink_sink.hpp"
using SinkMock = gentest::mock<symlinkprobe::Sink>;
[[maybe_unused]] inline SinkMock* kSinkMockPtr = nullptr;
