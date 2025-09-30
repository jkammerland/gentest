#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <vector>
#include <string>
#include <numeric>

// Example function to test
int add(int a, int b) { return a + b; }
bool is_even(int n) { return n % 2 == 0; }

// Example class to test
class Calculator {
public:
    int multiply(int a, int b) { return a * b; }
    double divide(double a, double b) { 
        if (b == 0) throw std::invalid_argument("Division by zero");
        return a / b; 
    }
};

TEST_CASE("Basic arithmetic operations") {
    SUBCASE("Addition") {
        CHECK(add(2, 3) == 5);
        CHECK(add(-1, 1) == 0);
        CHECK(add(0, 0) == 0);
        CHECK(add(100, 200) == 300);
    }
    
    SUBCASE("Even number check") {
        CHECK(is_even(2) == true);
        CHECK(is_even(4) == true);
        CHECK(is_even(3) == false);
        CHECK(is_even(5) == false);
        CHECK(is_even(0) == true);
        CHECK(is_even(-2) == true);
        CHECK(is_even(-3) == false);
    }
}

TEST_CASE("Vector operations") {
    std::vector<int> v{1, 2, 3, 4, 5};
    
    SUBCASE("Size and access") {
        REQUIRE(v.size() == 5);
        CHECK(v.front() == 1);
        CHECK(v.back() == 5);
        CHECK(v[2] == 3);
    }
    
    SUBCASE("Accumulation") {
        auto sum = std::accumulate(v.begin(), v.end(), 0);
        CHECK(sum == 15);
    }
    
    SUBCASE("Modification") {
        v.push_back(6);
        CHECK(v.size() == 6);
        CHECK(v.back() == 6);
        
        v.pop_back();
        v.pop_back();
        CHECK(v.size() == 4);
        CHECK(v.back() == 4);
    }
}

TEST_CASE("String operations") {
    std::string str = "Hello, World!";
    
    SUBCASE("Basic properties") {
        CHECK(str.length() == 13);
        CHECK(str.empty() == false);
        CHECK(str[0] == 'H');
    }
    
    SUBCASE("Substring operations") {
        CHECK(str.substr(0, 5) == "Hello");
        CHECK(str.substr(7, 5) == "World");
    }
    
    SUBCASE("Search operations") {
        CHECK(str.find("World") != std::string::npos);
        CHECK(str.find("World") == 7);
        CHECK(str.find("Foo") == std::string::npos);
    }
}

TEST_SUITE("Calculator tests") {
    TEST_CASE("Multiplication") {
        Calculator calc;
        
        CHECK(calc.multiply(3, 4) == 12);
        CHECK(calc.multiply(-2, 5) == -10);
        CHECK(calc.multiply(0, 100) == 0);
        CHECK(calc.multiply(1, 1) == 1);
    }
    
    TEST_CASE("Division") {
        Calculator calc;
        
        CHECK(calc.divide(10.0, 2.0) == doctest::Approx(5.0));
        CHECK(calc.divide(7.0, 2.0) == doctest::Approx(3.5));
        CHECK(calc.divide(1.0, 3.0) == doctest::Approx(0.333333).epsilon(0.001));
    }
    
    TEST_CASE("Division by zero throws") {
        Calculator calc;
        
        CHECK_THROWS_AS(calc.divide(5.0, 0.0), std::invalid_argument);
        CHECK_THROWS_WITH(calc.divide(5.0, 0.0), "Division by zero");
    }
}

// Parameterized tests using subcases
TEST_CASE("Parameterized even/odd test") {
    struct TestCase {
        int input;
        bool expected;
    };
    
    std::vector<TestCase> cases = {
        {2, true}, {4, true}, {6, true},
        {1, false}, {3, false}, {5, false}
    };
    
    for (const auto& tc : cases) {
        SUBCASE(("Input: " + std::to_string(tc.input)).c_str()) {
            CHECK(is_even(tc.input) == tc.expected);
        }
    }
}

// Template test cases
TEST_CASE_TEMPLATE("Numeric addition", T, int, long, float, double) {
    T a = 2;
    T b = 3;
    T result = a + b;
    CHECK(result == T(5));
}

// Test with custom messages
TEST_CASE("Tests with informative messages") {
    int value = 42;
    
    INFO("Testing value: ", value);
    CHECK(value > 0);
    
    CAPTURE(value); // Will be shown on failure
    CHECK(value == 42);
    
    // Multiple values can be captured
    int x = 10, y = 20;
    CAPTURE(x);
    CAPTURE(y);
    CHECK(x + y == 30);
}

// Fixture-like behavior using classes
class FixtureTest {
protected:
    std::vector<int> data;
    
    FixtureTest() : data{1, 2, 3, 4, 5} {
        // Setup code
    }
    
    ~FixtureTest() {
        // Teardown code
    }
};

TEST_CASE_FIXTURE(FixtureTest, "Test with fixture") {
    CHECK(data.size() == 5);
    CHECK(data[0] == 1);
    CHECK(data.back() == 5);
}

// Benchmarking (if enabled)
TEST_CASE("Simple benchmark" * doctest::skip(true) * doctest::timeout(1.0)) {
    // This test is skipped by default
    // Remove skip(true) to enable benchmarking
    
    int sum = 0;
    for (int i = 0; i < 1000000; ++i) {
        sum += i;
    }
    CHECK(sum > 0);
}

// Test decorators
TEST_CASE("Test with decorators"
    * doctest::description("This test demonstrates various decorators")
    * doctest::may_fail(false)
    * doctest::should_fail(false)
    * doctest::expected_failures(0)) {
    
    CHECK(true);
}