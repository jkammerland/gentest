export module fixture.validation_nested_scope_defs;

export import gentest.mock;

export namespace fixture {

struct Outer {
    struct Inner {
        virtual ~Inner() = default;
        virtual int value() = 0;
    };
};

namespace mocks {
using InnerMock = gentest::mock<Outer::Inner>;
}

} // namespace fixture
