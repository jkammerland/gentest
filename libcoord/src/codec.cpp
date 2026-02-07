#include "coord/codec.h"

#include <string>

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor.h>

namespace coord {

std::vector<std::byte> encode_message(const Message &msg, std::string *error) {
    std::vector<std::byte> buffer;
    auto enc = cbor::tags::make_encoder(buffer);
    auto result = enc(msg);
    if (!result) {
        if (error) {
            *error = std::string(cbor::tags::status_message(result.error()));
        }
        buffer.clear();
    }
    return buffer;
}

DecodeResult decode_message(std::span<const std::byte> data) {
    DecodeResult out{};
    std::vector<std::byte> buffer(data.begin(), data.end());
    auto dec = cbor::tags::make_decoder(buffer);
    Message msg{};
    auto result = dec(msg);
    if (!result) {
        out.ok = false;
        out.error = std::string(cbor::tags::status_message(result.error()));
        return out;
    }
    out.ok = true;
    out.message = std::move(msg);
    return out;
}

} // namespace coord
