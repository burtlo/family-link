#pragma once

#include <cstdint>

namespace lab04 {

constexpr bool valid_fixture_index(std::uint8_t fixture)
{
    return fixture < 3;
}

constexpr const char *fixture_name(std::uint8_t fixture)
{
    return fixture == 0 ? "corpus-a-short" :
           fixture == 1 ? "corpus-a-dense" :
           fixture == 2 ? "corpus-b" : nullptr;
}

} // namespace lab04
