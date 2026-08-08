#include "gentest/test.h"

namespace smoke::narrow_preamble {

[[using gentest: test("narrow/simple/plain")]]
void plain() {}

[[using gentest: test("narrow/simple/mixed_metadata"), fast, req("NARROW-42"), owner("headers"), skip("structural")]]
void mixed_metadata() {}

} // namespace smoke::narrow_preamble
