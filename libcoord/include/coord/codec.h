#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "coord/types.h"

namespace coord {

struct DecodeResult {
    bool ok{false};
    std::string error;
    Message message{};
};

std::vector<std::byte> encode_message(const Message &msg, std::string *error = nullptr);
DecodeResult decode_message(std::span<const std::byte> data);

} // namespace coord
