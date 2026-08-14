#pragma once

#include "gentest/attributes.h"

[[using gentest: test("explicit_mock_target/late_link_consumer")]]
void late_link_consumer();
