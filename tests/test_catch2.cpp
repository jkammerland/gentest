#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>
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

TEST_CASE("Basic arithmetic operations", "[math]") {
    SECTION("Addition") {
        REQUIRE(add(2, 3) == 5);
        REQUIRE(add(-1, 1) == 0);
        REQUIRE(add(0, 0) == 0);
    }
    
    SECTION("Even number check") {
        REQUIRE(is_even(2) == true);
        REQUIRE(is_even(4) == true);
        REQUIRE(is_even(3) == false);
        REQUIRE(is_even(5) == false);
        REQUIRE(is_even(0) == true);
    }
}

TEST_CASE("Vector operations", "[containers]") {
    std::vector<int> v{1, 2, 3, 4, 5};
    
    SECTION("Size and access") {
        REQUIRE(v.size() == 5);
        REQUIRE(v.front() == 1);
        REQUIRE(v.back() == 5);
        REQUIRE(v[2] == 3);
    }
    
    SECTION("Accumulation") {
        auto sum = std::accumulate(v.begin(), v.end(), 0);
        REQUIRE(sum == 15);
    }
    
    SECTION("Modification") {
        v.push_back(6);
        REQUIRE(v.size() == 6);
        REQUIRE(v.back() == 6);
        
        v.pop_back();
        v.pop_back();
        REQUIRE(v.size() == 4);
        REQUIRE(v.back() == 4);
    }
}

TEST_CASE("String operations", "[strings]") {
    std::string str = "Hello, World!";
    
    SECTION("Basic properties") {
        REQUIRE(str.length() == 13);
        REQUIRE(str.empty() == false);
        REQUIRE(str[0] == 'H');
    }
    
    SECTION("Substring operations") {
        REQUIRE(str.substr(0, 5) == "Hello");
        REQUIRE(str.substr(7, 5) == "World");
    }
    
    SECTION("String matchers") {
        using Catch::Matchers::StartsWith;
        using Catch::Matchers::EndsWith;
        using Catch::Matchers::ContainsSubstring;
        
        REQUIRE_THAT(str, StartsWith("Hello"));
        REQUIRE_THAT(str, EndsWith("World!"));
        REQUIRE_THAT(str, ContainsSubstring(", "));
    }
}

TEST_CASE("Calculator class", "[calculator]") {
    Calculator calc;
    
    SECTION("Multiplication") {
        REQUIRE(calc.multiply(3, 4) == 12);
        REQUIRE(calc.multiply(-2, 5) == -10);
        REQUIRE(calc.multiply(0, 100) == 0);
        REQUIRE(calc.multiply(1, 1) == 1);
    }
    
    SECTION("Division") {
        REQUIRE(calc.divide(10.0, 2.0) == Catch::Approx(5.0));
        REQUIRE(calc.divide(7.0, 2.0) == Catch::Approx(3.5));
        REQUIRE(calc.divide(1.0, 3.0) == Catch::Approx(0.333333).epsilon(0.001));
    }
    
    SECTION("Division by zero throws") {
        REQUIRE_THROWS_AS(calc.divide(5.0, 0.0), std::invalid_argument);
        REQUIRE_THROWS_WITH(calc.divide(5.0, 0.0), "Division by zero");
    }
}

TEST_CASE("Parameterized tests with generators", "[generators]") {
    auto value = GENERATE(1, 2, 3, 4, 5);
    
    SECTION("All generated values are positive") {
        REQUIRE(value > 0);
        REQUIRE(value <= 5);
    }
}

TEST_CASE("Table-driven tests", "[table]") {
    auto [input, expected] = GENERATE(table<int, bool>({
        {2, true},
        {4, true},
        {6, true},
        {1, false},
        {3, false},
        {5, false}
    }));
    
    REQUIRE(is_even(input) == expected);
}

TEST_CASE("Vector matchers", "[matchers]") {
    using Catch::Matchers::VectorContains;
    using Catch::Matchers::SizeIs;
    using Catch::Matchers::IsEmpty;
    
    std::vector<int> v{1, 2, 3, 4, 5};
    std::vector<int> empty;
    
    REQUIRE_THAT(v, VectorContains(3));
    REQUIRE_THAT(v, SizeIs(5));
    REQUIRE_THAT(empty, IsEmpty());
}