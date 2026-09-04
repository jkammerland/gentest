#ifdef GENTEST_RECORD_json
#include "json.hpp"
#else
#include "cbor.hpp"
#endif

int main(int argc, char **argv) { return gentest::run_all_tests(argc, argv); }
