#pragma once

/// @file
/// @brief Parcel cell for conduit-specific value types: `UlidCell`.
///
/// Flag sets ride on parcel's built-in `parcel::FlagSetCell` (wire kind
/// `"flag_set"`), pre-registered by `parcel::ParcelRegistry`'s commons builtins
/// — conduit no longer ships its own flag-set cell.

#include <ulid/ulid.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <parcel/parcel.h>

namespace conduit {

class SerializationError;  // forward — defined in serialization.hpp.

/// Parcel cell wrapping `ulid::Ulid`. JSON-encoded as a Crockford-base32 string
/// (matches `ulid::Ulid::string()` / `ulid::Ulid::from_string`).
class UlidCell : public parcel::BaseCell<UlidCell, ulid::Ulid> {
    using base_t = parcel::BaseCell<UlidCell, ulid::Ulid>;

public:
    using base_t::base_t;
    using base_t::operator=;

    static constexpr std::string_view kind_id = "conduit:ulid";

    [[nodiscard]] std::string to_string() const override {
        return this->value.string();
    }

    [[nodiscard]] parcel::json_t to_json() const override {
        parcel::json_t j{
            {parcel::ICell::KEY_KIND, kind_id},
            {parcel::ICell::KEY_VALUE, this->value.string()},
        };
        this->inject_display_info(j);
        return j;
    }

    static parcel::cell_t from_json(parcel::json_t const& j, parcel::ParcelRegistry const&) {
        if (!j.is_object()) {
            throw parcel::InvalidJsonException("Expected JSON object for UlidCell",
                                               std::string(kind_id));
        }
        const auto it_k = j.find(parcel::ICell::KEY_KIND);
        if (it_k == j.end() || !it_k->is_string()) {
            throw parcel::InvalidJsonException("UlidCell: missing/invalid 'k'",
                                               std::string(kind_id));
        }
        if (it_k->get<std::string_view>() != kind_id) {
            throw parcel::KindMismatchException("UlidCell: kind mismatch", std::string(kind_id));
        }
        const auto it_v = j.find(parcel::ICell::KEY_VALUE);
        if (it_v == j.end() || !it_v->is_string()) {
            throw parcel::InvalidJsonException("UlidCell: missing/invalid 'v' (expected string)",
                                               std::string(kind_id));
        }
        const auto parsed = ulid::Ulid::from_string(it_v->get<std::string>());
        if (!parsed.has_value()) {
            throw std::runtime_error{"UlidCell::from_json: invalid ULID string"};
        }
        auto cell = std::make_shared<UlidCell>(*parsed);
        base_t::absorb_display_info(j, cell);
        return cell;
    }

    static parcel::cell_type_descriptor_t descriptor() {
        static const auto d =
            std::make_shared<parcel::SimpleCellTypeDescriptor<UlidCell>>(parcel::DisplayInfo{
                .name = "Ulid",
                .description = "ULID identifier (Crockford-base32 string).",
            });
        return d;
    }
};

}  // namespace conduit

PARCEL_DEFAULT_CELL(::conduit::UlidCell);