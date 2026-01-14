// Types used by test signatures must be defined in a header so the wrapper TU can forward-declare the test.

struct LocalSignatureType {
    int value = 0;
};

[[using gentest: test]] void signature_type_defined_in_cpp(LocalSignatureType) {}

