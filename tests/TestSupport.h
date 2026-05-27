#pragma once

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

inline void require(bool condition, std::string_view message)
{
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

inline void requireNear(float actual, float expected, float tolerance, std::string_view message)
{
    if (std::abs(actual - expected) > tolerance) {
        std::cerr << "actual=" << actual << " expected=" << expected << " tolerance=" << tolerance << '\n';
        throw std::runtime_error(std::string(message));
    }
}

