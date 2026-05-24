/// JSON + CBOR envelope round-trip via `conduit::serialization`.

#include <conduit/conduit.hpp>

#include <iostream>
#include <string>

#include <parcel/parcel.h>

struct Telemetry : conduit::Event<Telemetry, "telemetry"> {
    std::string id;
    double value = 0.0;
    Telemetry() = default;
    Telemetry(std::string s, const double v) : id(std::move(s)), value(v) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Telemetry>& b) {
        return b.field<&Telemetry::id>("id").field<&Telemetry::value>("value");
    }
};

int main() {
    auto env = conduit::event(Telemetry{"t-1", 0.5}).metadata("source", "sensor-3").build();

    auto j = conduit::serialization::encode_json(env);
    auto bytes = conduit::serialization::encode_cbor(env);

    std::cout << "json size=" << j.dump().size() << " cbor size=" << bytes.size() << '\n';

    conduit::serialization::EventRegistry reg;
    reg.add<Telemetry>();
    auto round = reg.decode_json(j);
    auto recovered = round.payload_as<Telemetry>();
    std::cout << "recovered id=" << recovered->id << " value=" << recovered->value << '\n';
    return 0;
}
