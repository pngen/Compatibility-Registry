#pragma once
#include "compat/canonical.hpp"
#include "compat/id.hpp"
#include "compat/outcome.hpp"
#include "compat/sha256.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace compat {

enum class EvidenceKind : std::uint8_t {
    Measured,        // observed directly on hardware / execution
    Reported,        // reported by a source / tool/agent
    Derived,         // computed by derivation, not directly observed
    Validated,       // checked against an independent authority
    Reconstructed,   // rebuilt from stored state after recovery
    Unavailable
};

inline const char* evidence_kind_name(EvidenceKind k) noexcept {
    switch (k) {
        case EvidenceKind::Measured: return "measured";
        case EvidenceKind::Reported: return "reported";
        case EvidenceKind::Derived: return "derived";
        case EvidenceKind::Validated: return "validated";
        case EvidenceKind::Reconstructed: return "reconstructed";
        case EvidenceKind::Unavailable: return "unavailable";
    }
    return "?";
}

struct EvidenceRecord {
    EvidenceId id;
    Uuid subject;                 // the identity the evidence is about
    std::string field;            // dimension / field name
    Canon value;                  // evidence value
    std::string provenance;       // how it was obtained
    std::string source;           // producer identity (source name / tool)
    Generation source_generation;
    std::string observed_timestamp;
    EvidenceKind kind = EvidenceKind::Reported;
    bool validated = false;
    std::string digest_hex;       // digest of the evidence value where relevant
    std::string fingerprint() const {
        Canon::Record r = Canon::rec();
        Canon::put_uuid(r, 1, id.value);
        Canon::put_uuid(r, 2, subject);
        Canon::put_str(r, 3, field);
        Canon::put_record(r, 4, value.as_record());
        Canon::put_str(r, 5, provenance);
        Canon::put_str(r, 6, source);
        Canon::put_uint(r, 7, source_generation.value);
        return canonical_fingerprint_hex(Canon::mk_record(std::move(r)));
    }
};

struct ConstraintItem {
    std::string dimension;
    std::string left;
    std::string right;
    bool satisfied = false;
    std::string note;
};

struct CompatibilityDecision {
    Uuid decision_id;
    Uuid left;
    Uuid right;
    Outcome outcome = Outcome::Unknown;
    Uuid rule_id;                       // zero when no rule applied (e.g. exact identity)
    Generation rule_generation;
    Uuid policy_id;
    Generation policy_generation;
    std::vector<EvidenceId> evidence;
    std::vector<ConstraintItem> satisfied;
    std::vector<ConstraintItem> failed;
    std::vector<std::string> adaptations;
    std::vector<std::string> missing_evidence;
    std::vector<std::string> provenance;
    Generation registry_generation;
    bool counterfactual = false;        // derived, never authoritative
    std::string explanation;            // deterministic text
    Sha256::Digest digest{};

    // Deterministic decision digest over the semantic outcome fields (not the
    // free-form explanation), so replays reproduce it exactly.
    Sha256::Digest compute_digest() const;
    std::string digest_hex() const { return Sha256::hex(digest); }
};

} // namespace compat
