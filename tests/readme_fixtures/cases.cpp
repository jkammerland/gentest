#include "cases.hpp"

namespace fx::local {

void one(LocalCounter &local_fx, GlobalCounter &global_fx) {
    local_fx.touch();
    global_fx.touch();
}

} // namespace fx::local

namespace fx::shared {

void first(SuiteCounter &suite_fx, GlobalCounter &global_fx) {
    suite_fx.touch();
    global_fx.touch();
}

} // namespace fx::shared

namespace fx::shared {

void second(SuiteCounter &suite_fx, GlobalCounter &global_fx) {
    suite_fx.touch();
    global_fx.touch();
}

} // namespace fx::shared
