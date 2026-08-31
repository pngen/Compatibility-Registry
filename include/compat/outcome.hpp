#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace compat {

// Explicit compatibility outcomes. Unknown and insufficient evidence are distinct
// from one another and from incompatible.
enum class Outcome : std::uint8_t {
    Exact,                  // proven by identical canonical identity
    Compatible,             // proven by an explicit registry rule
    CompatibleWithAdaptation, // compatible after a stated set of adaptations
    Conditional,            // compatible only under conditions (rule-gated)
    Incompatible,
    Unknown,
    InsufficientEvidence
};

inline bool is_positive(const Outcome o) noexcept {
    return o == Outcome::Exact || o == Outcome::Compatible || o == Outcome::CompatibleWithAdaptation || o == Outcome::Conditional;
}

inline const char* outcome_name(const Outcome o) {
    switch (o) {
        case Outcome::Exact: return "EXACT";
        case Outcome::Compatible: return "COMPATIBLE";
        case Outcome::CompatibleWithAdaptation: return "COMPATIBLE_WITH_ADAPTATION";
        case Outcome::Conditional: return "CONDITIONAL";
        case Outcome::Incompatible: return "INCOMPATIBLE";
        case Outcome::Unknown: return "UNKNOWN";
        case Outcome::InsufficientEvidence: return "INSUFFICIENT_EVIDENCE";
    }
    throw std::runtime_error("outcome_name: unknown outcome");
}

inline Outcome outcome_from_name(std::string_view s) {
    std::string t(s);
    if (t == "EXACT") return Outcome::Exact;
    if (t == "COMPATIBLE") return Outcome::Compatible;
    if (t == "COMPATIBLE_WITH_ADAPTATION") return Outcome::CompatibleWithAdaptation;
    if (t == "CONDITIONAL") return Outcome::Conditional;
    if (t == "INCOMPATIBLE") return Outcome::Incompatible;
    if (t == "UNKNOWN") return Outcome::Unknown;
    if (t == "INSUFFICIENT_EVIDENCE") return Outcome::InsufficientEvidence;
    throw std::runtime_error("outcome_from_name: unknown outcome '" + t + "'");
}

} // namespace compat
