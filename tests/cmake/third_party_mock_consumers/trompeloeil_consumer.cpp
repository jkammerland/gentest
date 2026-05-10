#include "public/real_trompeloeil_mocks.hpp"

#include <memory>
#include <string>
#include <trompeloeil/mock.hpp>
#include <utility>
#include <vector>

int main() {
    {
        fixture::third_party::mocks::CalculatorMock mock;
        fixture::third_party::Calculator           *calculator = &mock;

        REQUIRE_CALL(mock, add(6, 7)).RETURN(13);
        if (calculator->add(6, 7) != 13) {
            return 1;
        }

        REQUIRE_CALL(mock, increment(41)).RETURN(42);
        if (mock.increment() != 42) {
            return 1;
        }

        REQUIRE_CALL(mock, name()).RETURN(std::string{"trompeloeil"});
        if (calculator->name() != "trompeloeil") {
            return 1;
        }
    }

    {
        using fixture::third_party::ResourceFactory;
        using fixture::third_party::aliases::IntPair;
        using fixture::third_party::aliases::PairBatch;

        fixture::third_party::mocks::ResourceFactoryMock mock;
        ResourceFactory                                 *factory = &mock;

        REQUIRE_CALL(mock, make("trompeloeil")).RETURN(std::make_unique<int>(42));
        auto owned = factory->make("trompeloeil");
        if (owned == nullptr || *owned != 42) {
            return 1;
        }

        int stored = 17;
        REQUIRE_CALL(mock, value()).RETURN(stored);
        if (&factory->value() != &stored) {
            return 1;
        }

        REQUIRE_CALL(mock, combine(trompeloeil::_)).WITH(_1 == PairBatch{IntPair{1, 2}, IntPair{3, 4}}).RETURN(IntPair{4, 6});
        if (factory->combine(PairBatch{IntPair{1, 2}, IntPair{3, 4}}) != IntPair{4, 6}) {
            return 1;
        }

        REQUIRE_CALL(mock, consume(trompeloeil::_)).WITH(*_1 == 9).RETURN(true);
        if (!factory->consume(std::make_unique<int>(9))) {
            return 1;
        }
    }

    {
        fixture::third_party::mocks::InheritedWorkflowMock mock;
        fixture::third_party::InheritedWorkflow           *workflow = &mock;

        REQUIRE_CALL(mock, inherited(14)).RETURN(21);
        if (workflow->inherited(14) != 21) {
            return 1;
        }

        REQUIRE_CALL(mock, label()).RETURN(std::string{"inherited"});
        if (workflow->label() != "inherited") {
            return 1;
        }
    }

    return 0;
}
