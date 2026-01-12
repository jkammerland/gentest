#pragma once

// Helper type for mocking checks in this suite (global to ease mock codegen).
struct SingleArg {
    void call(int) {}
};
