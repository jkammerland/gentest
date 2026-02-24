// Negative scenario: include a `.cpp` file that defines the mocked target.
// This must still be rejected; only header/header-unit definitions are allowed.

#include "nonvirtual_defined_in_cpp.cpp"
