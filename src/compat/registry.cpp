#include "compat/registry.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>
#include <functional>

namespace compat {

// ---------------------------------------------------------------------------
// Hash helpers
// ---------------------------------------------------------------------------
std::size_t DigestHash::operator()(const Sha256::Digest& d) const noexcept {
    std::size_t h = 14695981039346656037ull;
    for (unsigned char b : d) { h ^= static_cast<std::size_t>(b); h *= 1099511628211ull; }
    return h;
}

static Uuid digest_to_uuid(const Sha256::Digest& d) {
    Uuid::Bytes b{};
    std::copy_n(d.begin(), 16, b.begin());
    return Uuid(b);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
CompatibilityRegistry::CompatibilityRegistry() = default;

RegistryGeneration CompatibilityRegistry::current_registry_generation() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return RegistryGeneration{Generation{registry_gen_}};
}

// ---------------------------------------------------------------------------
// Dependency graph
// ---------------------------------------------------------------------------
void CompatibilityRegistry::add_dependency(Uuid id, const std::vector<Uuid>& deps) {
    // remove any prior edges from id, then add fresh ones. Reverse edges updated.
    if (deps_.count(id)) {
        for (const auto& d : deps_[id]) {
            auto it = reverse_.find(d);
            if (it != reverse_.end()) {
                auto& v = it->second;
                v.erase(std::remove(v.begin(), v.end(), id), v.end());
                if (v.empty()) reverse_.erase(it);
            }
        }
        deps_[id].clear();
    }
    for (const auto& d : deps) {
        if (d == id) continue; // self edge ignored for adjacency; cycle detect handles
        deps_[id].push_back(d);
        reverse_[d].push_back(id);
    }
}

void CompatibilityRegistry::remove_dependency(Uuid id) {
    if (deps_.count(id)) {
        for (const auto& d : deps_[id]) {
            auto it = reverse_.find(d);
            if (it != reverse_.end()) {
                auto& v = it->second;
                v.erase(std::remove(v.begin(), v.end(), id), v.end());
                if (v.empty()) reverse_.erase(it);
            }
        }
        deps_.erase(id);
    }
}

bool CompatibilityRegistry::dfs_cycle(const std::unordered_map<Uuid, std::vector<Uuid>, UuidHash>& g,
                                      const Uuid& n, std::unordered_map<Uuid, int, UuidHash>& color) {
    color[n] = 1; // visiting
    auto it = g.find(n);
    if (it != g.end()) {
        for (const auto& next : it->second) {
            auto c = color.find(next);
            if (c != color.end() && c->second == 1) return true;
            if (c == color.end() || c->second == 0) {
                if (dfs_cycle(g, next, color)) return true;
            }
        }
    }
    color[n] = 2; // done
    return false;
}

bool CompatibilityRegistry::has_cycle() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::unordered_map<Uuid, int, UuidHash> color;
    for (const auto& [id, d] : deps_) {
        if (color.find(id) == color.end() || color[id] == 0) {
            if (dfs_cycle(deps_, id, color)) return true;
        }
    }
    return false;
}

bool CompatibilityRegistry::create_cycle(Uuid from, const std::vector<Uuid>& to) const {
    // Build a temporary graph with the prospective edge and detect a cycle.
    auto g = deps_;
    for (const auto& d : to) { if (d != from) g[from].push_back(d); }
    std::sort(g[from].begin(), g[from].end());
    g[from].erase(std::unique(g[from].begin(), g[from].end()), g[from].end());
    std::unordered_map<Uuid, int, UuidHash> color;
    for (const auto& [id, dd] : g) {
        if (color.find(id) == color.end() || color[id] == 0) {
            if (dfs_cycle(g, id, color)) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Profile lifecycle
// ---------------------------------------------------------------------------
ProfileId CompatibilityRegistry::register_profile(ProfileKind kind, Canon payload, std::vector<Uuid> deps, std::string provenance) {
    ProfileRecord rec;
    rec.kind = kind;
    rec.id = Uuid::genseed();
    rec.payload = std::move(payload);
    rec.deps = std::move(deps);
    rec.provenance = std::move(provenance);
    rec.fingerprint = canonical_fingerprint_hex(rec.payload);
    return register_profile(std::move(rec));
}

ProfileId CompatibilityRegistry::register_profile(ProfileRecord rec) {
    std::lock_guard<std::mutex> lk(mutex_);
    rec.fingerprint = canonical_fingerprint_hex(rec.payload);
    // Idempotent: identical content + deps returns the existing identity.
    auto it = profiles_.find(rec.id);
    if (it != profiles_.end()) {
        auto& hist = it->second;
        const ProfileRecord& latest = hist.back();
        if (latest.active && latest.fingerprint == rec.fingerprint && latest.deps == rec.deps && latest.kind == rec.kind) {
            return ProfileId{rec.id};
        }
        // New generation: supersede the current latest in place (mark superseded).
        for (auto& p : hist) { if (p.active) { p.active = false; p.superseded = true; } }
    }
    // Assign a generation.
    Generation gen{1};
    if (it != profiles_.end()) {
        gen.value = it->second.back().generation.value + 1;
    }
    rec.generation = gen;
    // Reject dependency cycles before mutating authoritative state.
    if (create_cycle(rec.id, rec.deps)) {
        throw std::runtime_error("register_profile: dependency cycle rejected for " + rec.id.to_string());
    }
    profiles_[rec.id].push_back(rec);
    add_dependency(rec.id, rec.deps);
    registry_gen_++;
    return ProfileId{rec.id};
}

ProfileId CompatibilityRegistry::supersede_profile(ProfileKind kind, Uuid id, Canon payload, std::vector<Uuid> deps, std::string provenance) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = profiles_.find(id);
    if (it == profiles_.end()) {
        throw std::runtime_error("supersede_profile: unknown profile " + id.to_string());
    }
    ProfileRecord rec;
    rec.kind = kind;
    rec.id = id;
    rec.payload = std::move(payload);
    rec.deps = std::move(deps);
    rec.provenance = std::move(provenance);
    rec.fingerprint = canonical_fingerprint_hex(rec.payload);
    const ProfileRecord& latest = it->second.back();
    if (latest.active && latest.fingerprint == rec.fingerprint && latest.deps == rec.deps) {
        return ProfileId{id}; // no semantic change
    }
    for (auto& p : it->second) { if (p.active) { p.active = false; p.superseded = true; } }
    rec.generation = Generation{latest.generation.value + 1};
    remove_dependency(id);
    if (create_cycle(id, rec.deps)) {
        throw std::runtime_error("supersede_profile: dependency cycle rejected for " + id.to_string());
    }
    it->second.push_back(rec);
    add_dependency(id, rec.deps);
    registry_gen_++;
    return ProfileId{id};
}

bool CompatibilityRegistry::invalidate_profile(Uuid id, std::string reason) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = profiles_.find(id);
    if (it == profiles_.end()) return false;
    Invalidation inv;
    inv.target = id;
    inv.generation = Generation{it->second.back().generation.value};
    inv.reason = std::move(reason);
    inv.fingerprint_before = it->second.back().fingerprint;
    invalidations_.push_back(std::move(inv));
    for (auto& p : it->second) { p.active = false; p.superseded = true; }
    registry_gen_++;
    return true;
}

const ProfileRecord* CompatibilityRegistry::find_profile(Uuid id, bool include_history) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = profiles_.find(id);
    if (it == profiles_.end()) return nullptr;
    if (!include_history) {
        const ProfileRecord& last = it->second.back();
        if (last.active) return &last;
        // search newest active
        for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
            if (rit->active) return &*rit;
        }
        return nullptr; // invalidated
    }
    return &it->second.back();
}

std::vector<ProfileRecord> CompatibilityRegistry::history_of(Uuid id) const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<ProfileRecord> out;
    auto it = profiles_.find(id);
    if (it != profiles_.end()) out.assign(it->second.begin(), it->second.end());
    return out;
}

std::vector<ProfileRecord> CompatibilityRegistry::current_by_kind(ProfileKind kind) const {
    std::vector<ProfileRecord> out;
    for (const auto& [id, hist] : profiles_) {
        for (auto rit = hist.rbegin(); rit != hist.rend(); ++rit) {
            if (rit->active) { out.push_back(*rit); break; }
        }
    }
    out.erase(std::remove_if(out.begin(), out.end(), [&](const ProfileRecord& p){ return p.kind != kind; }), out.end());
    std::sort(out.begin(), out.end(), [](const ProfileRecord& a, const ProfileRecord& b){ return a.id < b.id; });
    (void)kind;
    return out;
}

std::vector<ProfileRecord> CompatibilityRegistry::query_by_kind(ProfileKind kind) const {
    std::lock_guard<std::mutex> lk(mutex_);
    return current_by_kind(kind);
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------
CompatibilityRuleId CompatibilityRegistry::register_rule(CompatibilityRule rule) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (rule.generation.value == 0) {
        auto it = rules_.find(rule.rule_id);
        if (it != rules_.end() && it->second.active) {
            rule.generation.value = it->second.rule.generation.value + 1;
        } else {
            rule.generation.value = ++rule_gen_;
        }
    }
    auto it = rules_.find(rule.rule_id);
    if (it != rules_.end()) it->second.superseded = true;
    RuleRecord rr; rr.rule = rule; rr.active = true;
    rules_[rule.rule_id] = rr;
    registry_gen_++;
    return rule.rule_id;
}

bool CompatibilityRegistry::supersede_rule(CompatibilityRuleId id, CompatibilityRule rule) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = rules_.find(id);
    if (it == rules_.end()) return false;
    rule.rule_id = id;
    rule.generation.value = it->second.rule.generation.value + 1;
    it->second.superseded = true;
    RuleRecord rr; rr.rule = rule; rr.active = true;
    rules_[id] = rr;
    registry_gen_++;
    return true;
}

bool CompatibilityRegistry::disable_rule(CompatibilityRuleId id) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = rules_.find(id);
    if (it == rules_.end()) return false;
    it->second.active = false;
    it->second.superseded = true;
    registry_gen_++;
    return true;
}

std::vector<CompatibilityRule> CompatibilityRegistry::active_rules() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<CompatibilityRule> out;
    for (const auto& [id, rr] : rules_) if (rr.active) out.push_back(rr.rule);
    return out;
}

// ---------------------------------------------------------------------------
// Evidence
// ---------------------------------------------------------------------------
void CompatibilityRegistry::register_evidence(EvidenceRecord ev) {
    std::lock_guard<std::mutex> lk(mutex_);
    evidence_[ev.id] = std::move(ev);
    evidence_gen_++;
    registry_gen_++;
}

const EvidenceRecord* CompatibilityRegistry::find_evidence(EvidenceId id) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = evidence_.find(id);
    if (it == evidence_.end()) return nullptr;
    return &it->second;
}

// ---------------------------------------------------------------------------
// Policy
// ---------------------------------------------------------------------------
void CompatibilityRegistry::set_policy(PolicyId policy) {
    std::lock_guard<std::mutex> lk(mutex_);
    policy_id_ = policy;
    registry_gen_++;
}

// ---------------------------------------------------------------------------
// Rule evaluation
// ---------------------------------------------------------------------------
struct RuleEval {
    bool definite = false;
    bool incompat = false;
    bool missing = false;
    Outcome outcome = Outcome::Unknown;
    std::vector<ConstraintItem> satisfied;
    std::vector<ConstraintItem> failed;
    std::vector<std::string> adaptations;
    std::vector<std::string> missing_evidence;
    const CompatibilityRule* rule = nullptr;
};


static bool constraint_known(const Constraint& c, const ProfileRecord& l, const ProfileRecord& r) {
    if (!resolve_profile_field(l.kind, l.payload, c.field)) return false;
    if (c.right_field.empty()) return true;
    return resolve_profile_field(r.kind, r.payload, c.right_field).has_value();
}

static RuleEval eval_rule(const CompatibilityRule& rule, const ProfileRecord& l, const ProfileRecord& r) {
    RuleEval e; e.rule = &rule;
    const ProfileKind lk = l.kind, rk = r.kind;

    // Explicit-incompatibility clauses (any match -> INCOMPATIBLE, defensively).
    for (const auto& c : rule.incompatible_with) {
        if (!constraint_known(c, l, r)) { e.missing = true; e.missing_evidence.push_back(c.field); continue; }
        std::string ok, notk;
        if (c.evaluate(l.payload, r.payload, lk, rk, ok, notk)) {
            e.definite = true; e.incompat = true; e.outcome = Outcome::Incompatible;
            e.failed.push_back(ConstraintItem{c.field, ok, notk, false, notk});
            return e;
        }
        e.satisfied.push_back(ConstraintItem{c.field, ok, notk, true, ok});
    }

    // Required conjunction.
    bool all = true;
    for (const auto& c : rule.required) {
        if (!constraint_known(c, l, r)) {
            e.missing = true;
            e.missing_evidence.push_back(c.field);
            e.failed.push_back(ConstraintItem{c.field, std::string(), std::string(), false, "missing evidence"});
            all = false;
            continue;
        }
        std::string ok, notk;
        if (c.evaluate(l.payload, r.payload, lk, rk, ok, notk)) {
            e.satisfied.push_back(ConstraintItem{c.field, ok, notk, true, ok});
        } else {
            e.failed.push_back(ConstraintItem{c.field, ok, notk, false, notk});
            all = false;
        }
    }
    if (!all) {
        if (e.missing) { e.definite = true; e.outcome = Outcome::InsufficientEvidence; }
        else { e.definite = false; e.outcome = Outcome::Unknown; }
        return e;
    }

    // Allowed disjunction (at least one; if provided).
    if (!rule.any_of.empty()) {
        bool any = false, any_missing = false;
        for (const auto& c : rule.any_of) {
            if (!constraint_known(c, l, r)) { any_missing = true; continue; }
            std::string ok, notk;
            if (c.evaluate(l.payload, r.payload, lk, rk, ok, notk)) { any = true; e.satisfied.push_back(ConstraintItem{c.field, ok, notk, true, ok}); break; }
            e.failed.push_back(ConstraintItem{c.field, ok, notk, false, notk});
        }
        if (!any) {
            if (any_missing) { e.missing = true; e.definite = true; e.outcome = Outcome::InsufficientEvidence; }
            else { e.definite = false; e.outcome = Outcome::Unknown; }
            return e;
        }
    }

    // Kind-specific result.
    switch (rule.kind) {
        case RuleKind::Conditional: {
            bool cond = true;
            for (const auto& c : rule.conditions) {
                if (!constraint_known(c, l, r)) { e.missing = true; e.missing_evidence.push_back(c.field); cond = false; continue; }
                std::string ok, notk;
                if (!c.evaluate(l.payload, r.payload, lk, rk, ok, notk)) { cond = false; e.failed.push_back(ConstraintItem{c.field, ok, notk, false, notk}); }
            }
            e.definite = true;
            e.outcome = cond ? rule.outcome : Outcome::Conditional;
            e.adaptations = rule.adaptations;
            return e;
        }
        case RuleKind::Adaptation: {
            e.definite = true; e.outcome = Outcome::CompatibleWithAdaptation; e.adaptations = rule.adaptations; return e;
        }
        case RuleKind::ExplicitIncompat: {
            e.definite = true; e.outcome = Outcome::Incompatible; e.adaptations = rule.adaptations; return e;
        }
        default: {
            e.definite = true; e.outcome = rule.outcome; e.adaptations = rule.adaptations; return e;
        }
    }
}

// ---------------------------------------------------------------------------
// Core deterministic evaluator (caller must NOT hold the lock)
// ---------------------------------------------------------------------------
CompatibilityDecision CompatibilityRegistry::evaluate_locked(Uuid left, Uuid right, bool requirement) const {
    CompatibilityDecision d;
    d.left = left; d.right = right;
    d.policy_id = policy_id_;
    d.registry_generation = Generation{registry_gen_};
    d.counterfactual = false;
    d.provenance.push_back("deterministic rule resolution");

    auto lit = profiles_.find(left);
    auto rit = profiles_.find(right);
    const ProfileRecord* l = nullptr; const ProfileRecord* r = nullptr;
    auto newest_active = [&](const auto& it) -> const ProfileRecord* {
        if (it == profiles_.end()) return nullptr;
        for (auto cit = it->second.rbegin(); cit != it->second.rend(); ++cit) if (cit->active) return &*cit;
        return nullptr;
    };
    l = newest_active(lit); r = newest_active(rit);
    if (!l || !r) {
        d.outcome = Outcome::Unknown;
        d.explanation = "pair cannot be evaluated: one or both profiles are absent or invalidated";
        d.digest = d.compute_digest();
        return d;
    }

    d.left = l->id; d.right = r->id;
    if (l->fingerprint == r->fingerprint) {
        d.outcome = Outcome::Exact;
        d.provenance.push_back("identical canonical identity (exact)");
        d.satisfied.push_back(ConstraintItem{"canonical_fingerprint", l->fingerprint, r->fingerprint, true, "identical"});
        d.digest = d.compute_digest();
        return d;
    }

    const std::string scope = requirement ? "requirement" : "pair";

    // Gather applicable active rules, deterministically ordered.
    std::vector<CompatibilityRule> cand;
    for (const auto& [id, rr] : rules_) {
        if (!rr.active) continue;
        const CompatibilityRule& rule = rr.rule;
        if (rule.scope != scope) continue;
        if (rule.left_kind && *rule.left_kind != l->kind) continue;
        if (rule.right_kind && *rule.right_kind != r->kind) continue;
        cand.push_back(rule);
    }
    std::sort(cand.begin(), cand.end(), [](const CompatibilityRule& a, const CompatibilityRule& b) {
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.rule_id < b.rule_id;
    });

    // The highest-priority applicable rule is the authoritative candidate even when
    // it yields Unknown/InsufficientEvidence, so the decision records which rule
    // generation governed the evaluation (for deterministic explanation/replay).
    const CompatibilityRule* authoritative = cand.empty() ? nullptr : &cand[0];

    std::vector<RuleEval> definite;
    bool any_rule_missing = false;
    for (const auto& rule : cand) {
        RuleEval e = eval_rule(rule, *l, *r);
        if (e.missing) any_rule_missing = true;
        if (e.definite && e.outcome != Outcome::Unknown) {
            definite.push_back(std::move(e));
        }
    }

    if (!definite.empty()) {
        const RuleEval& best = definite[0];
        d.outcome = best.outcome;
        if (best.rule) { d.rule_id = best.rule->rule_id.value; d.rule_generation = best.rule->generation; }
        d.satisfied = best.satisfied;
        d.failed = best.failed;
        d.adaptations = best.adaptations;
        d.missing_evidence = best.missing_evidence;
        if (best.rule) d.provenance.push_back("rule " + best.rule->rule_id.to_string() + " (priority " + std::to_string(best.rule->priority) + ")");
        // Explicit conflict surfacing.
        for (const auto& other : definite) {
            if (&other != &best && other.outcome != best.outcome) {
                d.provenance.push_back("conflict: rule " + (other.rule ? other.rule->rule_id.to_string() : std::string("?")) +
                                       " yielded " + outcome_name(other.outcome) + "; deterministic policy chose highest priority");
            }
        }
    }
    if (definite.empty() && authoritative) {
        d.rule_id = authoritative->rule_id.value;
        d.rule_generation = authoritative->generation;
        d.provenance.push_back("authoritative rule " + authoritative->rule_id.to_string() + " (generation " + std::to_string(authoritative->generation.value) + ")");
    }
    if (definite.empty()) {
        if (any_rule_missing) {
            d.outcome = Outcome::InsufficientEvidence;
            d.provenance.push_back("one or more rules needed evidence that is not present");
        } else {
            d.outcome = Outcome::Unknown;
            d.provenance.push_back("no rule establishes compatibility; profiles differ");
        }
    }

    // Deterministic decision identity from the evaluation inputs.
    Canon::Record ir = Canon::rec();
    Canon::put_uuid(ir, 1, d.left);
    Canon::put_uuid(ir, 2, d.right);
    Canon::put_uint(ir, 3, registry_gen_);
    Canon::put_str(ir, 4, scope);
    Canon::put_bool(ir, 5, requirement);
    Canon::put_uuid(ir, 6, policy_id_);
    d.decision_id = digest_to_uuid(canonical_fingerprint(Canon::mk_record(std::move(ir))));
    d.digest = d.compute_digest();
    if (!requirement) { /* authoritative only from pair */ }
    d.explanation = explain_text(d);
    return d;
}

CompatibilityDecision CompatibilityRegistry::evaluate_pair(Uuid left, Uuid right) const {
    std::lock_guard<std::mutex> lk(mutex_);
    CompatibilityDecision d = evaluate_locked(left, right, false);
    auto key = d.digest;
    decisions_[key] = d;
    return d;
}

CompatibilityDecision CompatibilityRegistry::evaluate_requirement(Uuid candidate, Uuid requirement) const {
    std::lock_guard<std::mutex> lk(mutex_);
    CompatibilityDecision d = evaluate_locked(candidate, requirement, true);
    decisions_[d.digest] = d;
    return d;
}

std::optional<CompatibilityDecision> CompatibilityRegistry::find_decision(CompatibilityDecisionId id) const {
    std::lock_guard<std::mutex> lk(mutex_);
    // decisions_ keyed by digest; also keep a small index by id.
    for (const auto& [dig, dec] : decisions_) { if (dec.decision_id == id.value) return dec; }
    return std::nullopt;
}

std::optional<CompatibilityDecision> CompatibilityRegistry::replay_decision(CompatibilityDecisionId id) const {
    std::lock_guard<std::mutex> lk(mutex_);
    for (const auto& [dig, dec] : decisions_) { if (dec.decision_id == id.value) return dec; }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Dependency queries / invalidation propagation
// ---------------------------------------------------------------------------
std::vector<Uuid> CompatibilityRegistry::dependencies_of(Uuid id) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = deps_.find(id);
    if (it == deps_.end()) return {};
    return it->second;
}

std::vector<Uuid> CompatibilityRegistry::dependents_of(Uuid id) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = reverse_.find(id);
    if (it == reverse_.end()) return {};
    std::vector<Uuid> out = it->second;
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void CompatibilityRegistry::propagate_invalidation(Uuid id) {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<Uuid> stack{id};
    std::vector<Uuid> seen;
    while (!stack.empty()) {
        Uuid cur = stack.back(); stack.pop_back();
        auto it = profiles_.find(cur);
        if (it != profiles_.end()) {
            for (auto& p : it->second) { p.active = false; p.superseded = true; }
            Invalidation inv; inv.target = cur;
            inv.generation = Generation{it->second.empty() ? 0u : it->second.back().generation.value};
            inv.reason = "propagated invalidation from dependency";
            inv.fingerprint_before = it->second.empty() ? std::string() : it->second.back().fingerprint;
            invalidations_.push_back(std::move(inv));
        }
        auto dit = reverse_.find(cur);
        if (dit != reverse_.end()) {
            for (const auto& n : dit->second) {
                if (std::find(seen.begin(), seen.end(), n) == seen.end()) { seen.push_back(n); stack.push_back(n); }
            }
        }
    }
    registry_gen_++;
}

// ---------------------------------------------------------------------------
// Counterfactual (derived only; never mutates authoritative state)
// ---------------------------------------------------------------------------
CompatibilityDecision CompatibilityRegistry::counterfactual_pair(Uuid left, Uuid right, std::vector<CounterfactualEdit> edits) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto newest = [&](Uuid id) -> const ProfileRecord* {
        auto it = profiles_.find(id);
        if (it == profiles_.end()) return nullptr;
        for (auto cit = it->second.rbegin(); cit != it->second.rend(); ++cit) if (cit->active) return &*cit;
        return nullptr;
    };
    const ProfileRecord* l = newest(left);
    const ProfileRecord* r = newest(right);
    CompatibilityDecision d;
    d.left = left; d.right = right; d.registry_generation = Generation{registry_gen_};
    d.policy_id = policy_id_;

    if (!l || !r) { d.outcome = Outcome::Unknown; d.counterfactual = true; d.provenance.push_back("counterfactual: profile absent"); d.digest = d.compute_digest(); return d; }

    ProfileRecord l2 = *l, r2 = *r;
    for (const auto& ed : edits) {
        // Split edit: apply to the side that owns the field, else left.
        ProfileRecord* target = &l2;
        if (resolve_profile_field(r->kind, r->payload, ed.field)) target = &r2;
        if (resolve_profile_field(target->kind, target->payload, ed.field)) {
            target->payload = apply_profile_field_edit(target->kind, target->payload, ed.field, ed.value);
            target->fingerprint = canonical_fingerprint_hex(target->payload);
        }
    }

    d.counterfactual = true;
    d.outcome = Outcome::Unknown;
    const std::string scope = "counterfactual";
    std::vector<CompatibilityRule> cand;
    for (const auto& [id, rr] : rules_) {
        if (!rr.active) continue;
        const CompatibilityRule& rule = rr.rule;
        if (rule.scope != "pair" && rule.scope != "counterfactual") continue;
        if (rule.left_kind && *rule.left_kind != l2.kind) continue;
        if (rule.right_kind && *rule.right_kind != r2.kind) continue;
        cand.push_back(rule);
    }
    std::sort(cand.begin(), cand.end(), [](const CompatibilityRule& a, const CompatibilityRule& b) {
        if (a.priority != b.priority) return a.priority > b.priority; return a.rule_id < b.rule_id;
    });
    std::vector<RuleEval> definite;
    bool any_missing = false;
    for (const auto& rule : cand) {
        RuleEval e = eval_rule(rule, l2, r2);
        if (e.missing) any_missing = true;
        if (e.definite && e.outcome != Outcome::Unknown) definite.push_back(std::move(e));
    }
    if (!definite.empty()) {
        const RuleEval& best = definite[0];
        d.outcome = best.outcome; d.satisfied = best.satisfied; d.failed = best.failed; d.adaptations = best.adaptations; d.missing_evidence = best.missing_evidence;
        if (best.rule) { d.rule_id = best.rule->rule_id.value; d.rule_generation = best.rule->generation; }
    } else if (any_missing) { d.outcome = Outcome::InsufficientEvidence; }
    else { d.outcome = Outcome::Unknown; }
    d.provenance.push_back("counterfactual (derived) -- never authoritative");
    d.digest = d.compute_digest();
    d.explanation = explain_text(d);
    return d;
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------
CompatibilityRegistry::Stats CompatibilityRegistry::stats() const {
    std::lock_guard<std::mutex> lk(mutex_);
    Stats s;
    s.profiles = profiles_.size();
    s.rules = rules_.size();
    s.evidence = evidence_.size();
    s.decisions = decisions_.size();
    s.invalidations = invalidations_.size();
    s.dependencies = deps_.size();
    return s;
}

// ---------------------------------------------------------------------------
// Decision digest (deterministic, independent of free-form text)
// ---------------------------------------------------------------------------
Sha256::Digest CompatibilityDecision::compute_digest() const {
    Canon::Record r = Canon::rec();
    Canon::put_uuid(r, 1, left);
    Canon::put_uuid(r, 2, right);
    Canon::put_uint(r, 3, static_cast<std::uint64_t>(outcome));
    Canon::put_uuid(r, 4, rule_id);
    Canon::put_uint(r, 5, rule_generation.value);
    Canon::put_uuid(r, 6, policy_id);
    Canon::put_uint(r, 7, policy_generation.value);
    Canon::put_uint(r, 8, registry_generation.value);
    Canon::put_bool(r, 9, counterfactual);
    { Canon::Seq s; for (auto& c : satisfied) { Canon::Record ci = Canon::rec(); Canon::put_str(ci,1,c.dimension); Canon::put_str(ci,2,c.left); Canon::put_str(ci,3,c.right); Canon::put_bool(ci,4,c.satisfied); Canon::put_str(ci,5,c.note); s.push_back(Canon::mk_record(std::move(ci))); } Canon::put_seq(r,10,std::move(s)); }
    { Canon::Seq s; for (auto& c : failed) { Canon::Record ci = Canon::rec(); Canon::put_str(ci,1,c.dimension); Canon::put_str(ci,2,c.left); Canon::put_str(ci,3,c.right); Canon::put_bool(ci,4,c.satisfied); Canon::put_str(ci,5,c.note); s.push_back(Canon::mk_record(std::move(ci))); } Canon::put_seq(r,11,std::move(s)); }
    { Canon::Seq s; for (auto& a : adaptations) s.push_back(Canon::mk_str(a)); Canon::put_seq(r,12,std::move(s)); }
    { Canon::Seq s; for (auto& m : missing_evidence) s.push_back(Canon::mk_str(m)); Canon::put_seq(r,13,std::move(s)); }
    return canonical_fingerprint(Canon::mk_record(std::move(r)));
}

// ---------------------------------------------------------------------------
// Explanations (deterministic text and JSON)
// ---------------------------------------------------------------------------
std::string CompatibilityRegistry::explain_text(const CompatibilityDecision& d) const {
    std::ostringstream ss;
    ss << "Compatibility decision " << d.decision_id.to_string() << "\n";
    ss << "  left:  " << d.left.to_string() << "\n";
    ss << "  right: " << d.right.to_string() << "\n";
    ss << "  outcome: " << outcome_name(d.outcome) << "\n";
    if (!d.rule_id.is_zero()) ss << "  rule: " << d.rule_id.to_string() << " (generation " << d.rule_generation.value << ")\n";
    if (!d.policy_id.is_zero()) ss << "  policy: " << d.policy_id.to_string() << "\n";
    ss << "  registry generation: " << d.registry_generation.value << "\n";
    if (!d.satisfied.empty()) {
        ss << "  satisfied constraints:\n";
        for (auto& c : d.satisfied) ss << "    + " << c.dimension << " [" << c.left << " == " << c.right << "] : " << c.note << "\n";
    }
    if (!d.failed.empty()) {
        ss << "  failed constraints:\n";
        for (auto& c : d.failed) ss << "    - " << c.dimension << " : " << c.note << "\n";
    }
    if (!d.adaptations.empty()) {
        ss << "  required adaptations:\n";
        for (auto& a : d.adaptations) ss << "    * " << a << "\n";
    }
    if (!d.missing_evidence.empty()) {
        ss << "  missing evidence:\n";
        for (auto& m : d.missing_evidence) ss << "    ? " << m << "\n";
    }
    if (d.counterfactual) ss << "  NOTE: derived counterfactual; not authoritative\n";
    ss << "  digest: " << d.digest_hex();
    return ss.str();
}

std::string CompatibilityRegistry::explain_json(const CompatibilityDecision& d) const {
    Canon::Record r = Canon::rec();
    Canon::put_str(r, 1, "decision");
    Canon::put_str(r, 2, d.decision_id.to_string());
    Canon::put_str(r, 3, outcome_name(d.outcome));
    Canon::put_str(r, 4, d.left.to_string());
    Canon::put_str(r, 5, d.right.to_string());
    if (!d.rule_id.is_zero()) Canon::put_str(r, 6, d.rule_id.to_string());
    { Canon::Seq s; for (auto& m : d.missing_evidence) s.push_back(Canon::mk_str(m)); Canon::put_seq(r, 7, std::move(s)); }
    { Canon::Seq s; for (auto& a : d.adaptations) s.push_back(Canon::mk_str(a)); Canon::put_seq(r, 8, std::move(s)); }
    { Canon::Seq s; for (auto& p : d.provenance) s.push_back(Canon::mk_str(p)); Canon::put_seq(r, 9, std::move(s)); }
    return canonical_to_json(Canon::mk_record(std::move(r)));
}

// ---------------------------------------------------------------------------
// Serialization helpers
// ---------------------------------------------------------------------------
static Canon enc_constraint(const Constraint& c) {
    Canon::Record r = Canon::rec();
    Canon::put_uint(r, 1, static_cast<std::uint64_t>(c.op));
    Canon::put_str(r, 2, c.field);
    Canon::put_str(r, 3, c.right_field);
    Canon::put(r, 4, c.expected);
    Canon::put(r, 5, c.expected2);
    { Canon::Seq s; for (auto& v : c.set) s.push_back(v); Canon::put_seq(r, 6, std::move(s)); }
    Canon::put_str(r, 7, c.note);
    return Canon::mk_record(std::move(r));
}

static Canon enc_constraint_item(const ConstraintItem& c) {
    Canon::Record r = Canon::rec();
    Canon::put_str(r, 1, c.dimension);
    Canon::put_str(r, 2, c.left);
    Canon::put_str(r, 3, c.right);
    Canon::put_bool(r, 4, c.satisfied);
    Canon::put_str(r, 5, c.note);
    return Canon::mk_record(std::move(r));
}

static Canon enc_profile(const ProfileRecord& p) {
    Canon::Record r = Canon::rec();
    Canon::put_uuid(r, 1, p.id);
    Canon::put_uint(r, 2, static_cast<std::uint64_t>(p.kind));
    Canon::put_uint(r, 3, p.generation.value);
    Canon::put_record(r, 4, p.payload.as_record());
    { Canon::Seq s; for (auto& d : p.deps) s.push_back(Canon::mk_uuid(d)); Canon::put_seq(r, 5, std::move(s)); }
    Canon::put_bool(r, 6, p.active);
    Canon::put_bool(r, 7, p.superseded);
    Canon::put_str(r, 8, p.provenance);
    Canon::put_str(r, 9, p.fingerprint);
    return Canon::mk_record(std::move(r));
}

static Canon enc_rule(const CompatibilityRule& rule, bool active) {
    Canon::Record r = Canon::rec();
    Canon::put_uuid(r, 1, rule.rule_id.value);
    Canon::put_uint(r, 2, rule.generation.value);
    Canon::put_str(r, 3, rule.domain);
    Canon::put_str(r, 4, rule.scope);
    Canon::put_int(r, 5, rule.priority);
    Canon::put_bool(r, 6, active);
    if (rule.left_kind) Canon::put_uint(r, 7, static_cast<std::uint64_t>(*rule.left_kind));
    if (rule.right_kind) Canon::put_uint(r, 8, static_cast<std::uint64_t>(*rule.right_kind));
    Canon::put_uint(r, 9, static_cast<std::uint64_t>(rule.kind));
    Canon::put_uint(r, 10, static_cast<std::uint64_t>(rule.outcome));
    { Canon::Seq s; for (auto& c : rule.required) s.push_back(enc_constraint(c)); Canon::put_seq(r, 11, std::move(s)); }
    { Canon::Seq s; for (auto& c : rule.any_of) s.push_back(enc_constraint(c)); Canon::put_seq(r, 12, std::move(s)); }
    { Canon::Seq s; for (auto& c : rule.incompatible_with) s.push_back(enc_constraint(c)); Canon::put_seq(r, 13, std::move(s)); }
    { Canon::Seq s; for (auto& c : rule.conditions) s.push_back(enc_constraint(c)); Canon::put_seq(r, 14, std::move(s)); }
    { Canon::Seq s; for (auto& a : rule.adaptations) s.push_back(Canon::mk_str(a)); Canon::put_seq(r, 15, std::move(s)); }
    Canon::put_str(r, 16, rule.provenance);
    return Canon::mk_record(std::move(r));
}

static Canon enc_evidence(const EvidenceRecord& e) {
    Canon::Record r = Canon::rec();
    Canon::put_uuid(r, 1, e.id.value);
    Canon::put_uuid(r, 2, e.subject);
    Canon::put_str(r, 3, e.field);
    Canon::put_record(r, 4, e.value.as_record());
    Canon::put_str(r, 5, e.provenance);
    Canon::put_str(r, 6, e.source);
    Canon::put_uint(r, 7, e.source_generation.value);
    Canon::put_str(r, 8, e.observed_timestamp);
    Canon::put_uint(r, 9, static_cast<std::uint64_t>(e.kind));
    Canon::put_bool(r, 10, e.validated);
    Canon::put_str(r, 11, e.digest_hex);
    return Canon::mk_record(std::move(r));
}

static Canon enc_invalidation(const Invalidation& iv) {
    Canon::Record r = Canon::rec();
    Canon::put_uuid(r, 1, iv.target);
    Canon::put_uint(r, 2, iv.generation.value);
    Canon::put_str(r, 3, iv.reason);
    Canon::put_str(r, 4, iv.fingerprint_before);
    return Canon::mk_record(std::move(r));
}

static Canon enc_decision(const CompatibilityDecision& d) {
    Canon::Record r = Canon::rec();
    Canon::put_uuid(r, 1, d.decision_id);
    Canon::put_uuid(r, 2, d.left);
    Canon::put_uuid(r, 3, d.right);
    Canon::put_uint(r, 4, static_cast<std::uint64_t>(d.outcome));
    Canon::put_uuid(r, 5, d.rule_id);
    Canon::put_uint(r, 6, d.rule_generation.value);
    Canon::put_uuid(r, 7, d.policy_id);
    Canon::put_uint(r, 8, d.policy_generation.value);
    { Canon::Seq s; for (auto& c : d.satisfied) s.push_back(enc_constraint_item(c)); Canon::put_seq(r, 9, std::move(s)); }
    { Canon::Seq s; for (auto& c : d.failed) s.push_back(enc_constraint_item(c)); Canon::put_seq(r, 10, std::move(s)); }
    { Canon::Seq s; for (auto& a : d.adaptations) s.push_back(Canon::mk_str(a)); Canon::put_seq(r, 11, std::move(s)); }
    { Canon::Seq s; for (auto& m : d.missing_evidence) s.push_back(Canon::mk_str(m)); Canon::put_seq(r, 12, std::move(s)); }
    { Canon::Seq s; for (auto& p : d.provenance) s.push_back(Canon::mk_str(p)); Canon::put_seq(r, 13, std::move(s)); }
    Canon::put_uint(r, 14, d.registry_generation.value);
    Canon::put_bool(r, 15, d.counterfactual);
    Canon::put_bytes(r, 16, std::string(reinterpret_cast<const char*>(d.digest.data()), d.digest.size()));
    return Canon::mk_record(std::move(r));
}

std::vector<std::uint8_t> CompatibilityRegistry::snapshot() const {
    std::lock_guard<std::mutex> lk(mutex_);
    Canon::Record root = Canon::rec();
    Canon::put_uint(root, 1, registry_gen_);
    Canon::put_uuid(root, 2, policy_id_);
    { Canon::Seq s; for (const auto& [id, hist] : profiles_) { (void)id; for (const auto& p : hist) s.push_back(enc_profile(p)); } Canon::put_seq(root, 3, std::move(s)); }
    { Canon::Seq s; for (const auto& [id, rr] : rules_) { (void)id; s.push_back(enc_rule(rr.rule, rr.active)); } Canon::put_seq(root, 4, std::move(s)); }
    { Canon::Seq s; for (const auto& [id, ev] : evidence_) { (void)id; s.push_back(enc_evidence(ev)); } Canon::put_seq(root, 5, std::move(s)); }
    { Canon::Seq s; for (const auto& iv : invalidations_) s.push_back(enc_invalidation(iv)); Canon::put_seq(root, 6, std::move(s)); }
    { Canon::Seq s; for (const auto& [dig, dec] : decisions_) { (void)dig; s.push_back(enc_decision(dec)); } Canon::put_seq(root, 7, std::move(s)); }

    std::vector<std::uint8_t> payload = canonical_encode(Canon::mk_record(std::move(root)));
    Sha256::Digest chk = Sha256::compute(payload.data(), payload.size());

    std::vector<std::uint8_t> out;
    out.reserve(payload.size() + 38);
    const char magic[5] = { 'C','R','R','E','G' };
    out.insert(out.end(), magic, magic + 5);
    out.push_back(1); // container version
    out.insert(out.end(), payload.begin(), payload.end());
    out.insert(out.end(), chk.begin(), chk.end());
    return out;
}

// ---------------------------------------------------------------------------
// Deserialization helpers
// ---------------------------------------------------------------------------
static Constraint dec_constraint(const Canon& c) {
    Constraint x;
    auto v = c.field(1); if (v && v->kind()==CanonKind::Uint) x.op = static_cast<PredOp>(v->as_uint());
    v = c.field(2); if (v && v->kind()==CanonKind::Str) x.field = v->as_string();
    v = c.field(3); if (v && v->kind()==CanonKind::Str) x.right_field = v->as_string();
    v = c.field(4); if (v) x.expected = *v;
    v = c.field(5); if (v) x.expected2 = *v;
    v = c.field(6); if (v && v->kind()==CanonKind::Sequence) { for (auto& e : v->as_seq()) x.set.push_back(e); }
    v = c.field(7); if (v && v->kind()==CanonKind::Str) x.note = v->as_string();
    return x;
}

static ConstraintItem dec_constraint_item(const Canon& c) {
    ConstraintItem x;
    auto v = c.field(1); if (v && v->kind()==CanonKind::Str) x.dimension = v->as_string();
    v = c.field(2); if (v && v->kind()==CanonKind::Str) x.left = v->as_string();
    v = c.field(3); if (v && v->kind()==CanonKind::Str) x.right = v->as_string();
    v = c.field(4); if (v && v->kind()==CanonKind::Bool) x.satisfied = v->as_bool();
    v = c.field(5); if (v && v->kind()==CanonKind::Str) x.note = v->as_string();
    return x;
}

static ProfileRecord dec_profile(const Canon& c) {
    ProfileRecord p;
    auto v = c.field(1); if (v && v->kind()==CanonKind::Uuid) p.id = v->as_uuid();
    v = c.field(2); if (v && v->kind()==CanonKind::Uint) p.kind = static_cast<ProfileKind>(v->as_uint());
    v = c.field(3); if (v && v->kind()==CanonKind::Uint) p.generation.value = v->as_uint();
    v = c.field(4); if (v && v->kind()==CanonKind::Record) p.payload = Canon::mk_record(v->as_record());
    v = c.field(5); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) if (e.kind()==CanonKind::Uuid) p.deps.push_back(e.as_uuid());
    v = c.field(6); if (v && v->kind()==CanonKind::Bool) p.active = v->as_bool();
    v = c.field(7); if (v && v->kind()==CanonKind::Bool) p.superseded = v->as_bool();
    v = c.field(8); if (v && v->kind()==CanonKind::Str) p.provenance = v->as_string();
    v = c.field(9); if (v && v->kind()==CanonKind::Str) p.fingerprint = v->as_string();
    return p;
}

static void dec_rule(const Canon& c, RuleRecord& rr) {
    auto v = c.field(1); if (v && v->kind()==CanonKind::Uuid) rr.rule.rule_id = CompatibilityRuleId(v->as_uuid());
    v = c.field(2); if (v && v->kind()==CanonKind::Uint) rr.rule.generation.value = v->as_uint();
    v = c.field(3); if (v && v->kind()==CanonKind::Str) rr.rule.domain = v->as_string();
    v = c.field(4); if (v && v->kind()==CanonKind::Str) rr.rule.scope = v->as_string();
    v = c.field(5); if (v && v->kind()==CanonKind::Int) rr.rule.priority = static_cast<int>(v->as_int());
    v = c.field(6); if (v && v->kind()==CanonKind::Bool) rr.active = v->as_bool();
    v = c.field(7); if (v && v->kind()==CanonKind::Uint) rr.rule.left_kind = static_cast<ProfileKind>(v->as_uint());
    v = c.field(8); if (v && v->kind()==CanonKind::Uint) rr.rule.right_kind = static_cast<ProfileKind>(v->as_uint());
    v = c.field(9); if (v && v->kind()==CanonKind::Uint) rr.rule.kind = static_cast<RuleKind>(v->as_uint());
    v = c.field(10); if (v && v->kind()==CanonKind::Uint) rr.rule.outcome = static_cast<Outcome>(v->as_uint());
    v = c.field(11); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) rr.rule.required.push_back(dec_constraint(e));
    v = c.field(12); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) rr.rule.any_of.push_back(dec_constraint(e));
    v = c.field(13); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) rr.rule.incompatible_with.push_back(dec_constraint(e));
    v = c.field(14); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) rr.rule.conditions.push_back(dec_constraint(e));
    v = c.field(15); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) if (e.kind()==CanonKind::Str) rr.rule.adaptations.push_back(e.as_string());
    v = c.field(16); if (v && v->kind()==CanonKind::Str) rr.rule.provenance = v->as_string();
}

static EvidenceRecord dec_evidence(const Canon& c) {
    EvidenceRecord e;
    auto v = c.field(1); if (v && v->kind()==CanonKind::Uuid) e.id = EvidenceId(v->as_uuid());
    v = c.field(2); if (v && v->kind()==CanonKind::Uuid) e.subject = v->as_uuid();
    v = c.field(3); if (v && v->kind()==CanonKind::Str) e.field = v->as_string();
    v = c.field(4); if (v && v->kind()==CanonKind::Record) e.value = Canon::mk_record(v->as_record());
    v = c.field(5); if (v && v->kind()==CanonKind::Str) e.provenance = v->as_string();
    v = c.field(6); if (v && v->kind()==CanonKind::Str) e.source = v->as_string();
    v = c.field(7); if (v && v->kind()==CanonKind::Uint) e.source_generation.value = v->as_uint();
    v = c.field(8); if (v && v->kind()==CanonKind::Str) e.observed_timestamp = v->as_string();
    v = c.field(9); if (v && v->kind()==CanonKind::Uint) e.kind = static_cast<EvidenceKind>(v->as_uint());
    v = c.field(10); if (v && v->kind()==CanonKind::Bool) e.validated = v->as_bool();
    v = c.field(11); if (v && v->kind()==CanonKind::Str) e.digest_hex = v->as_string();
    return e;
}

static Invalidation dec_invalidation(const Canon& c) {
    Invalidation iv;
    auto v = c.field(1); if (v && v->kind()==CanonKind::Uuid) iv.target = v->as_uuid();
    v = c.field(2); if (v && v->kind()==CanonKind::Uint) iv.generation.value = v->as_uint();
    v = c.field(3); if (v && v->kind()==CanonKind::Str) iv.reason = v->as_string();
    v = c.field(4); if (v && v->kind()==CanonKind::Str) iv.fingerprint_before = v->as_string();
    return iv;
}

static CompatibilityDecision dec_decision(const Canon& c) {
    CompatibilityDecision d;
    auto v = c.field(1); if (v && v->kind()==CanonKind::Uuid) d.decision_id = v->as_uuid();
    v = c.field(2); if (v && v->kind()==CanonKind::Uuid) d.left = v->as_uuid();
    v = c.field(3); if (v && v->kind()==CanonKind::Uuid) d.right = v->as_uuid();
    v = c.field(4); if (v && v->kind()==CanonKind::Uint) d.outcome = static_cast<Outcome>(v->as_uint());
    v = c.field(5); if (v && v->kind()==CanonKind::Uuid) d.rule_id = v->as_uuid();
    v = c.field(6); if (v && v->kind()==CanonKind::Uint) d.rule_generation.value = v->as_uint();
    v = c.field(7); if (v && v->kind()==CanonKind::Uuid) d.policy_id = v->as_uuid();
    v = c.field(8); if (v && v->kind()==CanonKind::Uint) d.policy_generation.value = v->as_uint();
    v = c.field(9); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) d.satisfied.push_back(dec_constraint_item(e));
    v = c.field(10); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) d.failed.push_back(dec_constraint_item(e));
    v = c.field(11); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) if (e.kind()==CanonKind::Str) d.adaptations.push_back(e.as_string());
    v = c.field(12); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) if (e.kind()==CanonKind::Str) d.missing_evidence.push_back(e.as_string());
    v = c.field(13); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) if (e.kind()==CanonKind::Str) d.provenance.push_back(e.as_string());
    v = c.field(14); if (v && v->kind()==CanonKind::Uint) d.registry_generation.value = v->as_uint();
    v = c.field(15); if (v && v->kind()==CanonKind::Bool) d.counterfactual = v->as_bool();
    v = c.field(16); if (v && v->kind()==CanonKind::Bytes) { const std::string& s = v->as_string(); std::copy_n(s.data(), std::min<std::size_t>(s.size(), 32), d.digest.begin()); }
    (void)c;
    return d;
}

std::unique_ptr<CompatibilityRegistry> CompatibilityRegistry::recover(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 5 + 1 + 32) throw std::runtime_error("recover: buffer too small");
    const char magic[5] = { 'C','R','R','E','G' };
    if (std::memcmp(bytes.data(), magic, 5) != 0) throw std::runtime_error("recover: bad container magic");
    if (bytes[5] != 1) throw std::runtime_error("recover: unsupported container version");
    const std::size_t payloadSize = bytes.size() - 5 - 1 - 32;
    std::span<const std::uint8_t> payload = bytes.subspan(5 + 1, payloadSize);
    std::span<const std::uint8_t> storedChk = bytes.subspan(5 + 1 + payloadSize, 32);
    Sha256::Digest recompute = Sha256::compute(payload.data(), payload.size());
    if (std::memcmp(recompute.data(), storedChk.data(), 32) != 0) throw std::runtime_error("recover: checksum mismatch (corruption)");

    Canon doc = canonical_decode(payload);
    if (doc.kind() != CanonKind::Record) throw std::runtime_error("recover: payload not a record");
    auto reg = std::make_unique<CompatibilityRegistry>();
    auto v = doc.field(1); if (v && v->kind()==CanonKind::Uint) reg->registry_gen_ = v->as_uint();
    v = doc.field(2); if (v && v->kind()==CanonKind::Uuid) reg->policy_id_ = v->as_uuid();
    v = doc.field(3); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) {
        ProfileRecord p = dec_profile(e);
        // verify fingerprint integrity
        if (!p.fingerprint.empty() && canonical_fingerprint_hex(p.payload) != p.fingerprint) throw std::runtime_error("recover: profile fingerprint mismatch (corruption)");
        reg->profiles_[p.id].push_back(std::move(p));
    }
    // rebuild dependency edges
    for (auto& [id, hist] : reg->profiles_) { for (auto& p : hist) reg->add_dependency(id, p.deps); }
    v = doc.field(4); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) {
        RuleRecord rr; dec_rule(e, rr); reg->rules_[rr.rule.rule_id] = std::move(rr);
    }
    v = doc.field(5); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) {
        EvidenceRecord ev = dec_evidence(e); reg->evidence_[ev.id] = std::move(ev);
    }
    v = doc.field(6); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) reg->invalidations_.push_back(dec_invalidation(e));
    v = doc.field(7); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) {
        CompatibilityDecision d = dec_decision(e); reg->decisions_[d.digest] = std::move(d);
    }
    if (reg->has_cycle()) throw std::runtime_error("recover: dependency cycle in snapshot");
    if (reg->profiles_.empty()) { /* allowed but sanity: empty registry */ }
    return reg;
}

bool CompatibilityRegistry::save(const std::string& path) const {
    std::vector<std::uint8_t> bytes = snapshot();
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

std::unique_ptr<CompatibilityRegistry> CompatibilityRegistry::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("load: cannot open " + path);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return recover(bytes);
}

// ---------------------------------------------------------------------------
// Matrix evaluation
// ---------------------------------------------------------------------------
MatrixResult CompatibilityRegistry::matrix(std::vector<Uuid> lefts, std::vector<Uuid> rights) const {
    std::lock_guard<std::mutex> lk(mutex_);
    MatrixResult mr;
    for (const auto& lft : lefts) {
        for (const auto& rgt : rights) {
            CompatibilityDecision d = evaluate_locked(lft, rgt, false);
            MatrixCell cell; cell.left = lft; cell.right = rgt; cell.outcome = d.outcome; cell.reason = outcome_name(d.outcome);
            mr.cells.push_back(std::move(cell));
            switch (d.outcome) {
                case Outcome::Exact: mr.exact++; break;
                case Outcome::Compatible: mr.compatible++; break;
                case Outcome::CompatibleWithAdaptation: mr.compatible_with_adaptation++; break;
                case Outcome::Conditional: mr.conditional++; break;
                case Outcome::Incompatible: mr.incompatible++; break;
                case Outcome::Unknown: mr.unknown++; break;
                case Outcome::InsufficientEvidence: mr.insufficient_evidence++; break;
            }
            mr.reason_counts[outcome_name(d.outcome)]++;
        }
    }
    return mr;
}

} // namespace compat
