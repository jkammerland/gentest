#include "import_only.hpp"

import gentest.cpp_source_imported_cases;

bool importOnlyProbe() {
    const cpp_source::ImportedValue input{42};
    return cpp_source::importedAnswer(input) == 42;
}
