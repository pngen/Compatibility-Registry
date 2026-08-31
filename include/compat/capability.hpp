#pragma once
#include "compat/canonical.hpp"
#include "compat/id.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <stdexcept>

namespace compat {

// A typed, deterministic capability set. Capability state is never mutated in
// place by authoritative change; a changed set produces a fresh generation / id.
//
// Tri-state semantics: a tag that is ABSENT is *unknown/unsupported-not-observed*,
// which is distinct from a present value of false. getters return std::nullopt
// for unknown so callers never collapse unknown onto false.
class CapabilitySet {
public:
    enum class Kind : std::uint8_t { Device, Backend, Runtime, Protocol, Toolchain, Kernel, Generic };

    CapabilitySetId id;
    Generation generation;
    Kind kind = Kind::Generic;
    CapabilitySet() = default;
    CapabilitySet(CapabilitySetId i, Generation g, Kind k) : id(i), generation(g), kind(k) {}

    // Deterministic content identity. Any change in stored state changes it.
    Canon to_canon() const {
        Canon::Record r = Canon::rec();
        Canon::put_uuid(r, 1, id.value);
        Canon::put_uint(r, 2, generation.value);
        Canon::put_uint(r, 3, static_cast<std::uint64_t>(kind));
        for (const auto& [tag, val] : caps_) Canon::put(r, tag, val);
        return Canon::mk_record(std::move(r));
    }

    bool has(std::uint32_t tag) const {
        auto it = find_(tag);
        return it != caps_.end();
    }

    std::optional<bool> get_bool(std::uint32_t tag) const {
        auto it = find_(tag);
        if (it == caps_.end() || it->second.kind() != CanonKind::Bool) return std::nullopt;
        return it->second.as_bool();
    }
    std::optional<std::uint64_t> get_uint(std::uint32_t tag) const {
        auto it = find_(tag);
        if (it == caps_.end() || it->second.kind() != CanonKind::Uint) return std::nullopt;
        return it->second.as_uint();
    }
    std::optional<double> get_float(std::uint32_t tag) const {
        auto it = find_(tag);
        if (it == caps_.end() || it->second.kind() != CanonKind::Float) return std::nullopt;
        return it->second.as_float();
    }
    std::optional<std::string> get_str(std::uint32_t tag) const {
        auto it = find_(tag);
        if (it == caps_.end() || it->second.kind() != CanonKind::Str) return std::nullopt;
        return it->second.as_string();
    }
    std::optional<std::uint64_t> get_uint_sub(std::uint32_t outer, std::uint32_t tag) const {
        auto it = find_(outer);
        if (it == caps_.end() || it->second.kind() != CanonKind::Record) return std::nullopt;
        const Canon* inner = it->second.field(tag);
        if (!inner || inner->kind() != CanonKind::Uint) return std::nullopt;
        return inner->as_uint();
    }

    void set_bool(std::uint32_t tag, bool v) { put_(tag, Canon::mk_bool(v)); }
    void set_uint(std::uint32_t tag, std::uint64_t v) { put_(tag, Canon::mk_uint(v)); }
    void set_float(std::uint32_t tag, double v) { put_(tag, Canon::mk_float(v)); }
    void set_str(std::uint32_t tag, std::string v) { put_(tag, Canon::mk_str(std::move(v))); }
    void set_record(std::uint32_t tag, Canon::Record r) { put_(tag, Canon::mk_record(std::move(r))); }
    void unset(std::uint32_t tag) {
        auto it = find_(tag);
        if (it != caps_.end()) caps_.erase(it);
    }

    std::size_t size() const { return caps_.size(); }
    const Canon::Record& all() const { return caps_; }

private:
    Canon::Record caps_;   // kept sorted by tag
    Canon::Record::const_iterator find_(std::uint32_t tag) const {
        return std::lower_bound(caps_.begin(), caps_.end(), tag, [](const auto& e, std::uint32_t t) { return e.first < t; });
    }
    void put_(std::uint32_t tag, Canon v) {
        auto it = std::lower_bound(caps_.begin(), caps_.end(), tag, [](const auto& e, std::uint32_t t) { return e.first < t; });
        if (it != caps_.end() && it->first == tag) it->second = std::move(v);
        else caps_.insert(it, std::make_pair(tag, std::move(v)));
    }
};

} // namespace compat
