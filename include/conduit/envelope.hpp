#pragma once

/// @file
/// @brief `EventEnvelope` — a parcel cell carrying conduit's envelope metadata
///        plus a polymorphic payload cell. Hand-written JSON layout.

#include <conduit/flags.hpp>
#include <conduit/metadata.hpp>

#include <ulid/ulid.h>

#include <chrono>
#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <parcel/parcel.h>

namespace conduit {

namespace detail {

/// Internal core shared between envelope copies — accessors return references
/// into this struct so transport pipelines can mutate timestamps/metadata on a
/// copy and see the change reflected on the original.
struct EnvelopeCore {
    ulid::Ulid id;
    flags::FlagSet flags;
    Metadata metadata;
    Timestamps timestamps{};
    std::optional<ulid::Ulid> correlation_id;
    std::optional<ulid::Ulid> causation_id;
    parcel::cell_t payload_cell;
};

[[nodiscard]] inline std::int64_t
to_ms_since_epoch(const std::chrono::system_clock::time_point tp) noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

[[nodiscard]] inline std::chrono::system_clock::time_point
from_ms_since_epoch(const std::int64_t ms) noexcept {
    return std::chrono::system_clock::time_point{std::chrono::milliseconds{ms}};
}

}  // namespace detail

/// Polymorphic envelope cell. Holds bookkeeping (id, flags, metadata,
/// timestamps, correlation/causation ids) plus a `parcel::cell_t` payload — so
/// any registered event cell can ride inside the same envelope kind.
///
/// Storage is a `shared_ptr<detail::EnvelopeCore>` so copies share the same
/// mutable core: transports that adjust `timestamps()`/`metadata()` on a local
/// copy update the originating envelope as well.
class EventEnvelope
    : public parcel::BaseCell<EventEnvelope, std::shared_ptr<detail::EnvelopeCore>> {
    using base_t = parcel::BaseCell<EventEnvelope, std::shared_ptr<detail::EnvelopeCore>>;

public:
    static constexpr std::string_view kind_id = "conduit:envelope";

    EventEnvelope() : base_t(std::make_shared<detail::EnvelopeCore>()) {}

    /// Build an envelope around a typed payload event. `T` must derive from
    /// `conduit::Event<T, Name>` (i.e. be an `ICell`-derived cell).
    template <typename T>
        requires std::derived_from<T, parcel::ICell>
    explicit EventEnvelope(T payload) : base_t(std::make_shared<detail::EnvelopeCore>()) {
        this->value->payload_cell = std::make_shared<T>(std::move(payload));
    }

    EventEnvelope(detail::EnvelopeCore core, parcel::cell_t payload)
        : base_t(std::make_shared<detail::EnvelopeCore>(std::move(core))) {
        this->value->payload_cell = std::move(payload);
    }

    explicit EventEnvelope(std::shared_ptr<detail::EnvelopeCore> core) : base_t(std::move(core)) {}

    // -- accessors ----------------------------------------------------------

    [[nodiscard]] const ulid::Ulid& id() const noexcept {
        return this->value->id;
    }

    [[nodiscard]] std::string_view name() const noexcept {
        if (!this->value || !this->value->payload_cell) {
            return {};
        }
        constexpr std::string_view prefix = "conduit:event:";
        const std::string_view k = this->value->payload_cell->kind();
        if (k.starts_with(prefix)) {
            return k.substr(prefix.size());
        }

        return k;
    }

    [[nodiscard]] const flags::FlagSet& flags() const noexcept {
        return this->value->flags;
    }
    [[nodiscard]] const Metadata& metadata() const noexcept {
        return this->value->metadata;
    }
    [[nodiscard]] const Timestamps& timestamps() const noexcept {
        return this->value->timestamps;
    }

    [[nodiscard]] const std::optional<ulid::Ulid>& correlation_id() const noexcept {
        return this->value->correlation_id;
    }
    [[nodiscard]] const std::optional<ulid::Ulid>& causation_id() const noexcept {
        return this->value->causation_id;
    }

    [[nodiscard]] const parcel::cell_t& payload_cell() const noexcept {
        return this->value->payload_cell;
    }

    /// Typed payload accessor: returns `shared_ptr<const T>` if the underlying
    /// payload is a `T` cell, or `nullptr` otherwise.
    template <typename T>
    [[nodiscard]] std::shared_ptr<const T> payload_as() const noexcept {
        if (!this->value || !this->value->payload_cell) {
            return nullptr;
        }
        return std::dynamic_pointer_cast<const T>(this->value->payload_cell);
    }

    // Transport-mutable views. Mutating through a non-const accessor on a
    // copy (e.g. `EventEnvelope local = v; local.timestamps().received_at = ...`)
    // updates the shared core, so all envelope copies observe the change.
    Timestamps& timestamps() noexcept {
        return this->value->timestamps;
    }
    Metadata& metadata() noexcept {
        return this->value->metadata;
    }
    flags::FlagSet& flags() noexcept {
        return this->value->flags;
    }

    [[nodiscard]] std::shared_ptr<detail::EnvelopeCore> core_ptr() const noexcept {
        return this->value;
    }

    [[nodiscard]] bool valid() const noexcept {
        return this->value != nullptr && this->value->payload_cell != nullptr;
    }

    // -- parcel cell overrides ---------------------------------------------

    [[nodiscard]] std::string to_string() const override {
        return std::string{name()};
    }

    [[nodiscard]] parcel::json_t to_json() const override;

    static parcel::cell_t from_json(parcel::json_t const& j, parcel::ParcelRegistry const& reg);

    [[nodiscard]] std::partial_ordering compare(parcel::ICell const& other) const override {
        if (const auto kc = this->kind() <=> other.kind(); kc != 0) {
            return kc;
        }
        // Envelope core/payload comparison isn't meaningful for delivery —
        // skip a deep compare to keep the cell instantiable.
        return std::partial_ordering::unordered;
    }

    static parcel::cell_type_descriptor_t descriptor() {
        static const auto d =
            std::make_shared<parcel::SimpleCellTypeDescriptor<EventEnvelope>>(parcel::DisplayInfo{
                .name = "EventEnvelope",
                .description = "Conduit event envelope (custom wire layout).",
            });
        return d;
    }
};

/// View alias: the envelope is already type-erased over its payload via
/// `parcel::cell_t`, so the legacy `EventEnvelopeView` name simply maps onto
/// `EventEnvelope` itself. Existing code that holds `const EventEnvelopeView&`
/// keeps working unchanged.
using EventEnvelopeView = EventEnvelope;

// ---------------------------------------------------------------------------
// JSON layout — hand-written, kept in one place for the whole envelope wire.
// ---------------------------------------------------------------------------

inline parcel::json_t EventEnvelope::to_json() const {
    if (!this->value || !this->value->payload_cell) {
        throw std::runtime_error{"EventEnvelope::to_json: envelope has no payload"};
    }
    const auto& [id, flags, metadata, timestamps, correlation_id, causation_id, payload_cell] =
        *this->value;

    parcel::json_t v_obj = parcel::json_t::object();
    v_obj["id"] = id.string();
    v_obj["name"] = std::string{name()};

    parcel::json_t flags_arr = parcel::json_t::array();
    for (const auto& f : flags) {
        flags_arr.push_back(std::string{f.name});
    }
    v_obj["flags"] = std::move(flags_arr);

    if (correlation_id.has_value()) {
        v_obj["correlation_id"] = correlation_id->string();
    }
    if (causation_id.has_value()) {
        v_obj["causation_id"] = causation_id->string();
    }

    v_obj["metadata"] = md::to_json(metadata);

    parcel::json_t ts = parcel::json_t::object();
    ts["created_at"] = detail::to_ms_since_epoch(timestamps.created_at);
    if (timestamps.published_at) {
        ts["published_at"] = detail::to_ms_since_epoch(*timestamps.published_at);
    }
    if (timestamps.received_at) {
        ts["received_at"] = detail::to_ms_since_epoch(*timestamps.received_at);
    }
    if (timestamps.delivered_at) {
        ts["delivered_at"] = detail::to_ms_since_epoch(*timestamps.delivered_at);
    }
    if (timestamps.failed_at) {
        ts["failed_at"] = detail::to_ms_since_epoch(*timestamps.failed_at);
    }
    v_obj["timestamps"] = std::move(ts);

    v_obj["payload"] = payload_cell->to_json();

    parcel::json_t out{
        {parcel::ICell::KEY_KIND, kind_id},
        {parcel::ICell::KEY_VALUE, std::move(v_obj)},
    };
    this->inject_display_info(out);
    return out;
}

inline parcel::cell_t EventEnvelope::from_json(parcel::json_t const& j,
                                               parcel::ParcelRegistry const& reg) {
    if (!j.is_object()) {
        throw parcel::InvalidJsonException("Expected JSON object for EventEnvelope",
                                           std::string(kind_id));
    }
    const auto it_k = j.find(parcel::ICell::KEY_KIND);
    if (it_k == j.end() || !it_k->is_string()) {
        throw parcel::InvalidJsonException("EventEnvelope: missing/invalid 'k'",
                                           std::string(kind_id));
    }
    if (it_k->get<std::string_view>() != kind_id) {
        throw parcel::KindMismatchException("EventEnvelope: kind mismatch", std::string(kind_id));
    }
    const auto it_v = j.find(parcel::ICell::KEY_VALUE);
    if (it_v == j.end() || !it_v->is_object()) {
        throw parcel::InvalidJsonException("EventEnvelope: missing/invalid 'v' (expected object)",
                                           std::string(kind_id));
    }
    const auto& v = *it_v;

    auto core = std::make_shared<detail::EnvelopeCore>();

    if (!v.contains("id") || !v.at("id").is_string()) {
        throw parcel::InvalidJsonException("EventEnvelope: missing/invalid 'id'",
                                           std::string(kind_id));
    }
    const auto id_opt = ulid::Ulid::from_string(v.at("id").get<std::string>());
    if (!id_opt.has_value()) {
        throw std::runtime_error{"EventEnvelope::from_json: invalid ULID"};
    }
    core->id = *id_opt;

    if (v.contains("correlation_id")) {
        const auto cid = ulid::Ulid::from_string(v.at("correlation_id").get<std::string>());
        if (!cid.has_value()) {
            throw std::runtime_error{"EventEnvelope::from_json: invalid correlation_id ULID"};
        }
        core->correlation_id = *cid;
    }
    if (v.contains("causation_id")) {
        const auto cid = ulid::Ulid::from_string(v.at("causation_id").get<std::string>());
        if (!cid.has_value()) {
            throw std::runtime_error{"EventEnvelope::from_json: invalid causation_id ULID"};
        }
        core->causation_id = *cid;
    }

    if (v.contains("flags")) {
        for (const auto& fn : v.at("flags")) {
            if (auto ref = comms::GlobalFlagRegistry::instance().find(fn.get<std::string>())) {
                core->flags.insert(*ref);
            }
        }
    }

    if (v.contains("metadata")) {
        if (const auto& md_json = v.at("metadata"); md_json.is_object()) {
            md::from_json(md_json, core->metadata);
        }
    }

    if (v.contains("timestamps")) {
        const auto& ts = v.at("timestamps");
        if (ts.contains("created_at")) {
            core->timestamps.created_at =
                detail::from_ms_since_epoch(ts.at("created_at").get<std::int64_t>());
        }
        if (ts.contains("published_at")) {
            core->timestamps.published_at =
                detail::from_ms_since_epoch(ts.at("published_at").get<std::int64_t>());
        }
        if (ts.contains("received_at")) {
            core->timestamps.received_at =
                detail::from_ms_since_epoch(ts.at("received_at").get<std::int64_t>());
        }
        if (ts.contains("delivered_at")) {
            core->timestamps.delivered_at =
                detail::from_ms_since_epoch(ts.at("delivered_at").get<std::int64_t>());
        }
        if (ts.contains("failed_at")) {
            core->timestamps.failed_at =
                detail::from_ms_since_epoch(ts.at("failed_at").get<std::int64_t>());
        }
    }

    if (!v.contains("payload")) {
        throw parcel::InvalidJsonException("EventEnvelope: missing 'payload'",
                                           std::string(kind_id));
    }
    core->payload_cell = reg.cell_from_json(v.at("payload"));

    auto out = std::make_shared<EventEnvelope>(std::move(core));
    base_t::absorb_display_info(j, out);
    return out;
}

}  // namespace conduit
