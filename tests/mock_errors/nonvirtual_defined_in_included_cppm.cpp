// Negative scenario: include a `.cppm` file that defines the mocked target.
// This must still be rejected; only header/header-unit definitions are allowed.

#include "nonvirtual_defined_in_cppm.cppm"
