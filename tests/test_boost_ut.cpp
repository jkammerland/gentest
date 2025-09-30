#include <boost/ut.hpp>
#include <vector>
#include <string>
#include <numeric>

using namespace boost::ut;

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

suite basic_tests = [] {
    "addition"_test = [] {
        expect(add(2, 3) == 5_i);
        expect(add(-1, 1) == 0_i);
        expect(add(0, 0) == 0_i);
    };

    "even number check"_test = [] {
        expect(is_even(2));
        expect(is_even(4));
        expect(!is_even(3));
        expect(!is_even(5));
        expect(is_even(0));
    };

    "vector operations"_test = [] {
        std::vector<int> v{1, 2, 3, 4, 5};
        
        expect(v.size() == 5_u);
        expect(v.front() == 1_i);
        expect(v.back() == 5_i);
        
        auto sum = std::accumulate(v.begin(), v.end(), 0);
        expect(sum == 15_i);
    };

    "string operations"_test = [] {
        std::string str = "Hello, World!";
        
        expect(str.length() == 13_u);
        expect(str.substr(0, 5) == "Hello");
        expect(str.find("World") != std::string::npos);
    };
};

suite calculator_tests = [] {
    Calculator calc;

    "multiplication"_test = [&calc] {
        expect(calc.multiply(3, 4) == 12_i);
        expect(calc.multiply(-2, 5) == -10_i);
        expect(calc.multiply(0, 100) == 0_i);
    };

    "division"_test = [&calc] {
        expect(calc.divide(10.0, 2.0) == 5.0_d);
        expect(calc.divide(7.0, 2.0) == 3.5_d);
        
        // Test exception throwing
        expect(throws<std::invalid_argument>([&calc] { calc.divide(5.0, 0.0); }));
    };
};

suite parameterized_tests = [] {
    "parameterized test"_test = [](auto value) {
        expect(value > 0) << "value should be positive";
    } | std::vector{1, 2, 3, 4, 5};

    "table test"_test = [] {
        struct TestCase {
            int input;
            bool expected;
        };
        
        std::vector<TestCase> cases = {
            {2, true}, {4, true}, {6, true},
            {1, false}, {3, false}, {5, false}
        };
        
        for (const auto& tc : cases) {
            expect(is_even(tc.input) == tc.expected) 
                << "Failed for input: " << tc.input;
        }
    };
};

int main() {
    // Run all tests
    return 0;
}