#include "gentest/mock.h"

namespace badnonvirtual {
struct Sink {
    void write(int) {}
};
} // namespace badnonvirtual

using SinkMock = gentest::mock<badnonvirtual::Sink>;
