#pragma once
#include "compat/canonical.hpp"
#include "compat/capability.hpp"
#include "compat/decision.hpp"
#include "compat/id.hpp"
#include "compat/outcome.hpp"
#include "compat/profile.hpp"
#include "compat/rule.hpp"
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace compat {

struct UuidHash {
    std::size_t operator()(const Uuid& u) const noexcept { return u.hash(); }
};

struct DigestHash {
    std::size_t operator()(const Sha256::Digest& d) const noexcept;
};

// A registered compatibility profile, immutable by generation. Superseding an
// identity never mutates a historical record; it adds a fresh generation.
struct ProfileRecord {
    ProfileKind kind = ProfileKind::Model;
    Uuid id;                    // ProfileId
    Generation generation;
    Canon payload;              // canonical descriptor
    std::vector<Uuid> deps;     // dependency identities
    bool active = true;
    bool superseded = false;
    std::string provenance;
    std::string fingerprint;    // content fingerprint (SHA-256 hex)
    bool operator==(const ProfileRecord& o) const { return id==o.id && generation==o.generation; }
};

struct RuleRecord {
    CompatibilityRule rule;
    bool active = true;
    bool superseded = false;
};

struct Invalidation {
    Uuid target;
    Generation generation;
    std::string reason;
    std::string fingerprint_before;
};

struct RegistrySnapshot {
    RegistryGeneration registry_generation;
    std::vector<ProfileRecord> profiles;
    std::vector<RuleRecord> rules;
    std::vector<EvidenceRecord> evidence;
    std::vector<Invalidation> invalidations;
    std::vector<CompatibilityDecision> decisions;   // historical decision cache
    Uuid policy_id;
};

struct MatrixCell {
    Uuid left;
    Uuid right;
    Outcome outcome;
    std::string reason;
};

struct MatrixResult {
    std::vector<MatrixCell> cells;
    std::size_t exact = 0, compatible = 0, compatible_with_adaptation = 0, conditional = 0,
                incompatible = 0, unknown = 0, insufficient_evidence = 0;
    std::map<std::string, std::size_t> reason_counts;
};

struct CounterfactualEdit {
    std::string field;    // dotted field on the target side
    Canon value;          // replacement value
};

// ---------------------------------------------------------------------------
// The authoritative, thread-safe CompatibilityRegistry.
// ---------------------------------------------------------------------------
class CompatibilityRegistry {
public:
    CompatibilityRegistry();

    // --- profile lifecycle ---
    ProfileId register_profile(ProfileKind kind, Canon payload, std::vector<Uuid> deps = {}, std::string provenance = {});
    ProfileId register_profile(ProfileRecord rec);
    // Supersede: same identity (by rec.id) receives a fresh generation; the old
    // generation is marked superseded. Returns the new profile id.
    ProfileId supersede_profile(ProfileKind kind, Uuid id, Canon payload, std::vector<Uuid> deps = {}, std::string provenance = {});
    bool invalidate_profile(Uuid id, std::string reason);

    const ProfileRecord* find_profile(Uuid id, bool include_history = false) const;
    std::vector<ProfileRecord> query_by_kind(ProfileKind kind) const;
    std::vector<ProfileRecord> history_of(Uuid id) const;

    // --- rules ---
    CompatibilityRuleId register_rule(CompatibilityRule rule);
    bool supersede_rule(CompatibilityRuleId id, CompatibilityRule rule);
    bool disable_rule(CompatibilityRuleId id);
    std::vector<CompatibilityRule> active_rules() const;

    // --- evidence ---
    void register_evidence(EvidenceRecord ev);
    const EvidenceRecord* find_evidence(EvidenceId id) const;

    // --- policy ---
    void set_policy(PolicyId policy);

    // --- evaluation ---
    CompatibilityDecision evaluate_pair(Uuid left, Uuid right) const;
    CompatibilityDecision evaluate_requirement(Uuid candidate, Uuid requirement) const;
    std::optional<CompatibilityDecision> find_decision(CompatibilityDecisionId id) const;
    std::optional<CompatibilityDecision> replay_decision(CompatibilityDecisionId id) const;

    // --- dependency graph ---
    std::vector<Uuid> dependencies_of(Uuid id) const;
    std::vector<Uuid> dependents_of(Uuid id) const;
    bool has_cycle() const;
    void propagate_invalidation(Uuid id);

    // --- counterfactual (derived, never mutates authoritative state) ---
    CompatibilityDecision counterfactual_pair(Uuid left, Uuid right, std::vector<CounterfactualEdit> edits) const;

    // --- persistence ---
    std::vector<std::uint8_t> snapshot() const;
    static std::unique_ptr<CompatibilityRegistry> recover(std::span<const std::uint8_t> bytes);
    bool save(const std::string& path) const;
    static std::unique_ptr<CompatibilityRegistry> load(const std::string& path);

    // --- matrix ---
    MatrixResult matrix(std::vector<Uuid> lefts, std::vector<Uuid> rights) const;

    // --- stats ---
    struct Stats { std::size_t profiles = 0, rules = 0, evidence = 0, decisions = 0, invalidations = 0, dependencies = 0; };
    Stats stats() const;
    RegistryGeneration current_registry_generation() const;

    // --- explain ---
    std::string explain_text(const CompatibilityDecision& d) const;
    std::string explain_json(const CompatibilityDecision& d) const;

private:
    mutable std::mutex mutex_;   // guards all mutable state; network never under this lock

    std::unordered_map<Uuid, std::deque<ProfileRecord>, UuidHash> profiles_;
    std::unordered_map<Uuid, RuleRecord, UuidHash> rules_;
    std::unordered_map<Uuid, EvidenceRecord, UuidHash> evidence_;
    mutable std::unordered_map<Sha256::Digest, CompatibilityDecision, DigestHash> decisions_;
    std::vector<Invalidation> invalidations_;
    std::unordered_map<Uuid, std::vector<Uuid>, UuidHash> deps_;      // id -> deps
    std::unordered_map<Uuid, std::vector<Uuid>, UuidHash> reverse_;   // id -> dependents
    std::uint64_t registry_gen_ = 0;
    Uuid policy_id_;
    std::uint64_t rule_gen_ = 0;
    std::uint64_t evidence_gen_ = 0;
    std::uint64_t capability_gen_ = 0;

    std::vector<ProfileRecord> current_by_kind(ProfileKind kind) const;
    CompatibilityDecision evaluate_locked(Uuid left, Uuid right, bool requirement) const;
    void add_dependency(Uuid id, const std::vector<Uuid>& deps);
    void remove_dependency(Uuid id);
    static bool dfs_cycle(const std::unordered_map<Uuid, std::vector<Uuid>, UuidHash>& g, const Uuid& n,
                          std::unordered_map<Uuid, int, UuidHash>& color);
    bool create_cycle(Uuid from, const std::vector<Uuid>& to) const;
};

} // namespace compat
