#include "cases.hpp"

#include "gentest/assertions.h"
#include "gentest/runner.h"
#include "public/mock_argument_shapes_mocks.hpp"

#include <array>
#include <sstream>
#include <string>

namespace fixture {

void incomparable_parameters() {
    gentest::mock<Sink> mock_sink;
    int                 seen = 0;
    EXPECT_CALL(mock_sink, subscribe).times(1).invokes([&](const std::function<void()> &callback) { callback(); });
    EXPECT_CALL(mock_sink, send).times(1).where_call([](std::span<const int> values) { return values.size() == 3; });
    EXPECT_CALL(mock_sink, dump).times(1).invokes([](std::ostream &out) { out << "dumped"; });
    EXPECT_CALL(mock_sink, put).times(1).where([](const NoEq &value) { return value.value == 4; });
    EXPECT_CALL(mock_sink, handle).times(1).invokes([&](const IAbstract &shape) { seen += shape.id(); });
    EXPECT_CALL(mock_sink, mixed).times(1).where(gentest::match::Any(), gentest::match::Eq(2));

    Sink &sink = mock_sink;
    sink.subscribe([&] { seen += 1; });
    const std::array<int, 3> values{1, 2, 3};
    sink.send(values);
    std::ostringstream out;
    sink.dump(out);
    sink.put(NoEq{4});
    sink.handle(ConcreteShape{10});
    sink.mixed(NoEq{}, 2);

    gentest::expect_eq(seen, 11, "callbacks and abstract arguments reach the actions");
    gentest::expect_eq(out.str(), std::string("dumped"), "stream arguments are passed by reference");
}

void with_owns_text() {
    gentest::mock<Text> mock_text;
    {
        // The expected values die before the calls: with() must own them.
        std::string       temporary = "payload-" + std::to_string(12345678) + "-long-enough-to-leave-the-small-buffer";
        const std::string label     = std::string("la") + "bel";
        EXPECT_CALL(mock_text, write).times(1).with(std::string(temporary));
        EXPECT_CALL(mock_text, label).times(1).with(label.c_str());
        EXPECT_CALL(mock_text, name).times(1).with(std::string_view(temporary));
        temporary.assign(temporary.size(), 'x');
    }

    const std::string expected = "payload-12345678-long-enough-to-leave-the-small-buffer";
    const std::string label    = std::string("lab") + "el";
    Text             &text     = mock_text;
    text.write(expected);
    text.label(label.c_str()); // equal content at a different address
    text.name(expected);
}

void with_converts_values() {
    gentest::mock<Text> mock_text;
    EXPECT_CALL(mock_text, strong).times(1).with(7);
    EXPECT_CALL(mock_text, scaled).times(1).with(2, 3);
    EXPECT_CALL(mock_text, label).times(1).with(nullptr);

    Text &text = mock_text;
    text.strong(Strong{7});
    text.scaled(2.0, 3U);
    text.label(nullptr);
}

void text_mismatch_death() {
    gentest::mock<Text> mock_text;
    EXPECT_CALL(mock_text, label).times(1).with("expected");
    mock_text.label("actual");
}

} // namespace fixture
