#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>

class Uuid128 {
public:
    std::array<uint8_t, 16> data{};

    Uuid128() = default;

    explicit Uuid128(const uint8_t* bytes) {
        std::memcpy(data.data(), bytes, 16);
    }

    bool operator==(const Uuid128& other) const {
        return data == other.data;
    }

    bool operator<(const Uuid128& other) const {
        return data < other.data;  // for use in std::map
    }

    std::string to_string() const {
        std::ostringstream ss;
        for (auto b : data) {
            ss << std::hex << std::setfill('0') << std::setw(2) << (int)b;
        }
        return ss.str();
    }

    const uint8_t* raw() const { return data.data(); }
};