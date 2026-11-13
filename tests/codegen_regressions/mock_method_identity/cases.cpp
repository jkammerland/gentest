#include "cases.hpp"

#include "gentest/assertions.h"
#include "gentest/runner.h"
#include "public/mock_method_identity_mocks.hpp"

namespace fixture {

void multiple_inheritance_signatures() {
    gentest::mock<IStream> mock_stream;
    int                    written = 0;
    EXPECT_CALL(mock_stream, write).times(1).with(7).invokes([&](int value) { written = value; });
    EXPECT_CALL(mock_stream, read).times(2).returns(42);

    IStream &stream = mock_stream;
    gentest::expect_eq(stream.read(), 42, "read() takes its own expectation");
    stream.write(7);
    gentest::expect_eq(stream.read(), 42, "read() is not consumed by write()");
    gentest::expect_eq(written, 7, "write() takes its own expectation");
}

void multiple_inheritance_same_signature() {
    gentest::mock<IBoth> mock_both;
    EXPECT_CALL(mock_both, left).times(1).returns(1);
    gentest::expect(mock_both, &IBoth::right).times(1).returns(2);

    IBoth &both = mock_both;
    gentest::expect_eq(both.right(), 2, "right() result");
    gentest::expect_eq(both.left(), 1, "left() result");
}

void inherited_methods() {
    gentest::mock<InheritedService> mock_service;
    EXPECT_CALL(mock_service, visible).times(1).returns(10);
    EXPECT_CALL(mock_service, plain).times(1).returns(20);
    gentest::expect(mock_service, static_cast<int (InheritedBase::*)(int)>(&InheritedService::inherited)).times(1).returns(30);
    gentest::expect(mock_service, static_cast<long (InheritedService::*)(long)>(&InheritedService::inherited)).times(1).returns(40L);

    InheritedBase &base = mock_service;
    gentest::expect_eq(mock_service.visible(1), 10, "own method");
    gentest::expect_eq(base.plain(1), 20, "inherited const noexcept method");
    gentest::expect_eq(base.inherited(1), 30, "inherited overload selected by its declaring class");
    gentest::expect_eq(base.inherited(1L), 40L, "inherited overload selected through the target class");
}

// An expectation stored under an identity that a differently typed call also
// reaches must be rejected, never reinterpreted.
void foreign_signature_death() {
    using gentest::detail::mocking::InstanceState;
    using gentest::detail::mocking::MethodIdentity;

    InstanceState state;
    const auto    id = MethodIdentity::from(&IWriter::write);
    (void)state.push_expectation<void, int>(id, "::fixture::IWriter::write");
    gentest::expect_eq(state.dispatch<int>(id, "::fixture::IReader::read"), 0, "a rejected call returns a default value");
}

} // namespace fixture
