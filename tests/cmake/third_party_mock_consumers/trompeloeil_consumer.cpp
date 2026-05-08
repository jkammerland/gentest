#include "public/real_trompeloeil_mocks.hpp"

#include <string>
#include <trompeloeil/mock.hpp>

int main() {
    {
        fixture::third_party::mocks::CalculatorMock mock;
        fixture::third_party::Calculator           *calculator = &mock;

        REQUIRE_CALL(mock, add(6, 7)).RETURN(13);
        if (calculator->add(6, 7) != 13) {
            return 1;
        }

        REQUIRE_CALL(mock, name()).RETURN(std::string{"trompeloeil"});
        if (calculator->name() != "trompeloeil") {
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
