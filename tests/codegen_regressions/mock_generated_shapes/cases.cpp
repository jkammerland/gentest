#include "cases.hpp"

#include "gentest/assertions.h"
#include "gentest/runner.h"
#include "public/mock_generated_shapes_mocks.hpp"

#include <array>
#include <string>

namespace fixture {

void parameter_names() {
    gentest::mock<Authorizer> mock_auth;
    EXPECT_CALL(mock_auth, authorize).times(1).with("secret").returns(true);
    EXPECT_CALL(mock_auth, refresh).times(1).with("old", "new").returns(true);
    EXPECT_CALL(mock_auth, internals).times(1).with(1, 2, 3, 4).returns(10);

    const Authorizer &auth = mock_auth;
    gentest::expect(mock_auth.authorize("secret"), "authorize(token)");
    gentest::expect(mock_auth.refresh("old", "new"), "refresh(token, fallback_token)");
    gentest::expect_eq(auth.internals(1, 2, 3, 4), 10, "parameters named like generated members");
}

void parameter_packs() {
    gentest::mock<Emitter> mock_emitter(1, 2.0, "three");
    int                    emitted = 0;
    gentest::expect<&Emitter::emit<int, std::string>>(mock_emitter, "::fixture::Emitter::emit<int, std::string>")
        .times(1)
        .invokes([&](int &&number, std::string &&text) { emitted = number + static_cast<int>(text.size()); });
    gentest::expect<&Emitter::count<long, char>>(mock_emitter, "::fixture::Emitter::count<long, char>")
        .times(1)
        .with(5, 6L, 'x')
        .returns(7);

    mock_emitter.emit(4, std::string("abc"));
    gentest::expect_eq(emitted, 7, "pack arguments are forwarded");
    gentest::expect_eq(mock_emitter.count(5, 6L, 'x'), 7, "by-value pack arguments are forwarded");
}

void operator_templates() {
    gentest::mock<Bus> mock_bus;
    Bus                bus;
    int                shifted = 0;
    EXPECT_CALL(mock_bus, operator<< <int>).times(1).invokes([&](const int &value) -> Bus & {
        shifted = value;
        return bus;
    });
    EXPECT_CALL(mock_bus, operator< <int>).times(1).with(3).returns(true);

    mock_bus << 9;
    gentest::expect_eq(shifted, 9, "operator<< <int> dispatches");
    gentest::expect(mock_bus < 3, "operator< <int> dispatches");
}

void qualified_expressions() {
    gentest::mock<Slots> mock_slots;
    int                  sum = 0;
    EXPECT_CALL(mock_slots, fill<int>).times(1).invokes([&](const std::array<int, 3> &values) {
        for (const int value : values)
            sum += value;
    });
    EXPECT_CALL(mock_slots, grow<int>).times(1).invokes([&](const std::array<int, 4> &values) { sum += values[3]; });
    EXPECT_CALL(mock_slots, call<int>).times(1).invokes([&](void (*callback)(int) noexcept, int value) {
        callback(value);
        sum += value;
    });

    mock_slots.fill(std::array<int, 3>{1, 2, 3});
    mock_slots.grow(std::array<int, 4>{0, 0, 0, 10});
    mock_slots.call<int>([](int) noexcept {}, 100);
    gentest::expect_eq(sum, 116, "expressions in member template signatures resolve inside namespace gentest");
}

} // namespace fixture
