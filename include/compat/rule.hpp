#pragma once
#include "compat/canonical.hpp"
#include "compat/id.hpp"
#include "compat/outcome.hpp"
#include "compat/profile.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <stdexcept>

namespace compat {

std::optional<Canon> resolve_profile_field(ProfileKind kind, const Canon& profile, std::string_view dotted_name);

// Rebuild a profile payload with a single (top-level) field replaced by value.
// Returns a new canonical record; does not mutate the input.
Canon apply_profile_field_edit(ProfileKind kind, const Canon& payload, const std::string& dotted_field, const Canon& value);

enum class RuleKind : std::uint8_t {
    Equality,
    ExactIdentity,
    SetMembership,
    NumericRange,
    MinCapability,
    MaxCapability,
    VersionRelation,
    SemVerRelation,
    ArchFamily,
    FeatureRequirement,
    Conjunction,
    Disjunction,
    ExplicitIncompat,
    Adaptation,
    Conditional,
    Dependency,
    PolicyOverride
};

enum class PredOp : std::uint8_t {
    Eq, Ne, InSet, Min, Max, VersionLt, VersionLe, VersionGt, VersionGe,
    ArchFamily, FeatureReq, Truthy
};

inline const char* pred_op_name(PredOp p) noexcept {
    switch (p) {
        case PredOp::Eq: return "eq"; case PredOp::Ne: return "ne";
        case PredOp::InSet: return "in_set"; case PredOp::Min: return "min";
        case PredOp::Max: return "max"; case PredOp::VersionLt: return "version_lt";
        case PredOp::VersionLe: return "version_le"; case PredOp::VersionGt: return "version_gt";
        case PredOp::VersionGe: return "version_ge"; case PredOp::ArchFamily: return "arch_family";
        case PredOp::FeatureReq: return "feature_req"; case PredOp::Truthy: return "truthy";
    }
    return "?";
}

struct Constraint {
    PredOp op = PredOp::Eq;
    std::string field;
    std::string right_field;
    Canon expected;
    Canon expected2;
    std::vector<Canon> set;
    std::string note;

    bool evaluate(const Canon& left, const Canon& right, ProfileKind left_kind, ProfileKind right_kind,
                  std::string& why_ok, std::string& why_not) const;
};

struct CompatibilityRule {
    CompatibilityRuleId rule_id;
    Generation generation;
    std::string domain;
    std::string scope;
    int priority = 0;
    bool active = true;
    RuleKind kind = RuleKind::Equality;
    std::optional<ProfileKind> left_kind;   // <empty> = any
    std::optional<ProfileKind> right_kind;  // <empty> = any
    std::vector<Constraint> required;
    std::vector<Constraint> any_of;
    std::vector<Constraint> incompatible_with;
    std::vector<Constraint> conditions;
    std::vector<std::string> adaptations;
    Outcome outcome = Outcome::Compatible;
    std::string provenance;
};

inline bool rule_bool(const Canon& c) { return c.kind()==CanonKind::Bool && c.as_bool(); }

} // namespace compat
