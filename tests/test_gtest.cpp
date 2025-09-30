#include <gtest/gtest.h>
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

// Basic test cases
TEST(BasicMathTest, Addition) {
    EXPECT_EQ(add(2, 3), 5);
    EXPECT_EQ(add(-1, 1), 0);
    EXPECT_EQ(add(0, 0), 0);
    EXPECT_EQ(add(100, 200), 300);
}

TEST(BasicMathTest, EvenNumberCheck) {
    EXPECT_TRUE(is_even(2));
    EXPECT_TRUE(is_even(4));
    EXPECT_FALSE(is_even(3));
    EXPECT_FALSE(is_even(5));
    EXPECT_TRUE(is_even(0));
    EXPECT_TRUE(is_even(-2));
    EXPECT_FALSE(is_even(-3));
}

// Test with vectors
TEST(VectorTest, BasicOperations) {
    std::vector<int> v{1, 2, 3, 4, 5};
    
    ASSERT_EQ(v.size(), 5u);
    EXPECT_EQ(v.front(), 1);
    EXPECT_EQ(v.back(), 5);
    EXPECT_EQ(v[2], 3);
    
    auto sum = std::accumulate(v.begin(), v.end(), 0);
    EXPECT_EQ(sum, 15);
}

TEST(VectorTest, Modification) {
    std::vector<int> v{1, 2, 3};
    
    v.push_back(4);
    EXPECT_EQ(v.size(), 4u);
    EXPECT_EQ(v.back(), 4);
    
    v.pop_back();
    v.pop_back();
    EXPECT_EQ(v.size(), 2u);
    EXPECT_EQ(v.back(), 2);
}

// String tests
TEST(StringTest, BasicOperations) {
    std::string str = "Hello, World!";
    
    EXPECT_EQ(str.length(), 13u);
    EXPECT_FALSE(str.empty());
    EXPECT_EQ(str[0], 'H');
    EXPECT_EQ(str.substr(0, 5), "Hello");
    EXPECT_EQ(str.substr(7, 5), "World");
}

TEST(StringTest, SearchOperations) {
    std::string str = "Hello, World!";
    
    EXPECT_NE(str.find("World"), std::string::npos);
    EXPECT_EQ(str.find("World"), 7u);
    EXPECT_EQ(str.find("Foo"), std::string::npos);
    
    EXPECT_TRUE(str.starts_with("Hello"));
    EXPECT_TRUE(str.ends_with("World!"));
}

// Test fixture for Calculator class
class CalculatorTest : public ::testing::Test {
protected:
    Calculator calc;
    
    void SetUp() override {
        // Setup code if needed
    }
    
    void TearDown() override {
        // Cleanup code if needed
    }
};

TEST_F(CalculatorTest, Multiplication) {
    EXPECT_EQ(calc.multiply(3, 4), 12);
    EXPECT_EQ(calc.multiply(-2, 5), -10);
    EXPECT_EQ(calc.multiply(0, 100), 0);
    EXPECT_EQ(calc.multiply(1, 1), 1);
}

TEST_F(CalculatorTest, Division) {
    EXPECT_DOUBLE_EQ(calc.divide(10.0, 2.0), 5.0);
    EXPECT_DOUBLE_EQ(calc.divide(7.0, 2.0), 3.5);
    EXPECT_NEAR(calc.divide(1.0, 3.0), 0.333333, 0.001);
}

TEST_F(CalculatorTest, DivisionByZeroThrows) {
    EXPECT_THROW(calc.divide(5.0, 0.0), std::invalid_argument);
    
    // Test specific exception message
    try {
        calc.divide(5.0, 0.0);
        FAIL() << "Expected std::invalid_argument";
    } catch (const std::invalid_argument& e) {
        EXPECT_STREQ(e.what(), "Division by zero");
    } catch (...) {
        FAIL() << "Expected std::invalid_argument";
    }
}

// Parameterized tests
class EvenNumberTest : public ::testing::TestWithParam<std::pair<int, bool>> {
};

TEST_P(EvenNumberTest, CheckEvenOdd) {
    auto [input, expected] = GetParam();
    EXPECT_EQ(is_even(input), expected);
}

INSTANTIATE_TEST_SUITE_P(EvenOddTests, EvenNumberTest,
    ::testing::Values(
        std::make_pair(2, true),
        std::make_pair(4, true),
        std::make_pair(6, true),
        std::make_pair(1, false),
        std::make_pair(3, false),
        std::make_pair(5, false)
    )
);

// Typed tests
template <typename T>
class NumericTest : public ::testing::Test {
public:
    T add(T a, T b) { return a + b; }
};

using MyTypes = ::testing::Types<int, long, float, double>;
TYPED_TEST_SUITE(NumericTest, MyTypes);

TYPED_TEST(NumericTest, Addition) {
    TypeParam a = 2;
    TypeParam b = 3;
    TypeParam result = this->add(a, b);
    EXPECT_EQ(result, TypeParam(5));
}

// Death tests (for testing fatal errors/crashes)
void fatal_function(bool should_crash) {
    if (should_crash) {
        abort();
    }
}

TEST(DeathTest, FatalFunction) {
    EXPECT_DEATH(fatal_function(true), "");
    // This should not die
    EXPECT_NO_FATAL_FAILURE(fatal_function(false));
}

// Custom assertion
::testing::AssertionResult IsPositive(int n) {
    if (n > 0) {
        return ::testing::AssertionSuccess() << n << " is positive";
    } else {
        return ::testing::AssertionFailure() << n << " is not positive";
    }
}

TEST(CustomAssertionTest, PositiveNumbers) {
    EXPECT_TRUE(IsPositive(5));
    EXPECT_FALSE(IsPositive(-3));
    EXPECT_FALSE(IsPositive(0));
}