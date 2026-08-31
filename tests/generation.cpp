// generation.cpp -- generation / invalidation / dependency proof.
// Proves that supersede creates fresh generations while preserving history, that
// fresh evaluation uses the latest rule generation, that historical decisions
// replay deterministically, that generation changes alter decision identity, and
// that invalidations propagate through the dependency graph (cycles rejected).
#include "testutil.hpp"
#include "compat/compat.hpp"
#include <cstdint>
#include <string>
#include <vector>

using namespace compat;

static Canon kernelPayload(const std::string& idHex, const std::string& cc,
                           const std::string& dtype, const std::string& arch) {
    KernelProfile p;
    p.kernel_artifact_id = KernelArtifactId::from_string(idHex);
    p.operation = "add";
    p.architecture = arch;
    p.compute_capability = cc;
    p.abi = "cuda";
    p.runtime = "cudart";
    p.compiler = "nvcc";
    p.compiler_version = "12.9";
    p.dtype = dtype;
    p.layout = "aos";
    p.shape_spec = "[N]";
    p.quantization = "none";
    p.interface = "c";
    p.generation = 0;
    p.artifact_digest = "digest-" + cc;
    return p.to_canon();
}

static Canon devicePayload(const std::string& idHex, const std::string& cc,
                           const std::string& arch, std::uint64_t memGB) {
    DeviceProfile p;
    p.device_id = DeviceId::from_string(idHex);
    p.vendor = "NVIDIA";
    p.architecture = arch;
    p.compute_capability = cc;
    p.memory_model = "global";
    p.total_memory = memGB * 1024ull * 1024ull * 1024ull;
    p.supported_dtypes = { "f16", "f32", "bf16" };
    p.instruction_classes = { arch };
    p.runtime_min = "12.0";
    p.driver_min = "12.0";
    return p.to_canon();
}

static CompatibilityRule versionRule(const std::string& idHex, int prio) {
    CompatibilityRule rule;
    rule.rule_id = CompatibilityRuleId::from_string(idHex);
    rule.scope = "pair";
    rule.left_kind = ProfileKind::Kernel;
    rule.right_kind = ProfileKind::Device;
    rule.kind = RuleKind::Equality;
    rule.outcome = Outcome::Compatible;
    rule.priority = prio;
    Constraint c;
    c.op = PredOp::VersionLe;
    c.field = "compute_capability";
    c.right_field = "compute_capability";
    rule.required.push_back(c);
    return rule;
}

// ---- supersede creates a new generation and keeps history --------------------

static void test_supersede_history() {
    TEST("supersede creates a new generation and keeps history");
    CompatibilityRegistry reg;
    ProfileId k1 = reg.register_profile(ProfileKind::Kernel, kernelPayload("00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100"));
    std::vector<ProfileRecord> h0 = reg.history_of(k1.value);
    CHECK(h0.size() == 1);
    CHECK(h0[0].generation.value == 1);
    CHECK(h0[0].active);
    CHECK(!h0[0].superseded);

    ProfileId k1b = reg.supersede_profile(ProfileKind::Kernel, k1.value,
                                          kernelPayload("00000000-0000-0000-0000-000000000001", "10.0", "bf16", "sm_100"));
    CHECK(k1b.value == k1.value);                 // same identity
    std::vector<ProfileRecord> h1 = reg.history_of(k1.value);
    CHECK(h1.size() == 2);                        // history retained
    CHECK(h1[0].generation.value == 1);
    CHECK(h1[1].generation.value == 2);
    CHECK(h1[0].superseded);                      // old generation marked superseded
    CHECK(!h1[0].active);
    CHECK(h1[1].active);                          // new generation active
    CHECK(h1[1].fingerprint != h1[0].fingerprint);

    const ProfileRecord* cur = reg.find_profile(k1.value);
    CHECK(cur != nullptr);
    CHECK(cur->generation.value == 2);
    CHECK(cur->fingerprint == h1[1].fingerprint);
}

// ---- old rule generation does not govern fresh evaluation ---------------------

static void test_rule_generation_supersede() {
    TEST("old rule generation does not govern fresh evaluation");
    CompatibilityRegistry reg;
    ProfileId k1 = reg.register_profile(ProfileKind::Kernel, kernelPayload("00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100"));
    ProfileId d1 = reg.register_profile(ProfileKind::Device, devicePayload("00000000-0000-0000-0000-000000000002", "12.0", "sm_120", 32));

    CompatibilityRule r1;
    r1.rule_id = CompatibilityRuleId::from_string("00000000-0000-0000-0000-0000000000aa");
    r1.scope = "pair";
    r1.left_kind = ProfileKind::Kernel;
    r1.right_kind = ProfileKind::Device;
    r1.kind = RuleKind::Equality;
    r1.outcome = Outcome::Compatible;
    Constraint c1; c1.op = PredOp::Eq; c1.field = "dtype"; c1.expected = Canon::mk_str("f16");
    r1.required.push_back(c1);
    reg.register_rule(r1);

    CompatibilityDecision before = reg.evaluate_pair(k1.value, d1.value);
    CHECK(before.outcome == Outcome::Compatible);

    // Supersede the rule: now it requires dtype == f32.
    CompatibilityRule r2;
    r2.rule_id = r1.rule_id;
    r2.scope = "pair";
    r2.left_kind = ProfileKind::Kernel;
    r2.right_kind = ProfileKind::Device;
    r2.kind = RuleKind::Equality;
    r2.outcome = Outcome::Compatible;
    Constraint c2; c2.op = PredOp::Eq; c2.field = "dtype"; c2.expected = Canon::mk_str("f32");
    r2.required.push_back(c2);
    bool ok = reg.supersede_rule(r1.rule_id, r2);
    CHECK(ok);
    CHECK(reg.active_rules().size() == 1);

    CompatibilityDecision after = reg.evaluate_pair(k1.value, d1.value);
    CHECK(after.outcome == Outcome::Unknown);     // f16 no longer satisfies f32
    CHECK(after.rule_id == r2.rule_id.value);
    CHECK(after.rule_generation.value == before.rule_generation.value + 1);
}

// ---- historical decision reproducible ------------------------------------------

static void test_replay() {
    TEST("historical decision reproducible");
    CompatibilityRegistry reg;
    ProfileId k1 = reg.register_profile(ProfileKind::Kernel, kernelPayload("00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100"));
    ProfileId d1 = reg.register_profile(ProfileKind::Device, devicePayload("00000000-0000-0000-0000-000000000002", "12.0", "sm_120", 32));
    reg.register_rule(versionRule("00000000-0000-0000-0000-0000000000aa", 0));

    CompatibilityDecision d = reg.evaluate_pair(k1.value, d1.value);
    CHECK(d.outcome == Outcome::Compatible);

    auto rp = reg.replay_decision(CompatibilityDecisionId(d.decision_id));
    CHECK(rp.has_value());
    CHECK(rp->decision_id == d.decision_id);
    CHECK(rp->digest == d.digest);
    CHECK(rp->outcome == d.outcome);

    auto fd = reg.find_decision(CompatibilityDecisionId(d.decision_id));
    CHECK(fd.has_value());
    CHECK(fd->digest == d.digest);
    CHECK(fd->decision_id == d.decision_id);

    // Replaying the same identity reproduces the same digest.
    CHECK(rp->digest_hex() == d.digest_hex());
}

// ---- re-evaluation under a new generation produces a new decision_id -----------

static void test_new_decision_generation() {
    TEST("re-evaluation under new generation produces a NEW decision_id");
    CompatibilityRegistry reg;
    ProfileId k1 = reg.register_profile(ProfileKind::Kernel, kernelPayload("00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100"));
    ProfileId d1 = reg.register_profile(ProfileKind::Device, devicePayload("00000000-0000-0000-0000-000000000002", "12.0", "sm_120", 32));
    CompatibilityRule r = versionRule("00000000-0000-0000-0000-0000000000aa", 0);
    reg.register_rule(r);

    CompatibilityDecision first = reg.evaluate_pair(k1.value, d1.value);
    CHECK(first.outcome == Outcome::Compatible);

    // Change the kernel in place -> registry generation bumps.
    reg.supersede_profile(ProfileKind::Kernel, k1.value,
                          kernelPayload("00000000-0000-0000-0000-000000000001", "11.0", "bf16", "sm_110"));
    CompatibilityDecision second = reg.evaluate_pair(k1.value, d1.value);
    CHECK(second.decision_id != first.decision_id);        // decision identity changes
    CHECK(second.registry_generation.value > first.registry_generation.value);
    CHECK(second.outcome == Outcome::Compatible);          // 11 <= 12 still holds
}

// ---- invalidation propagation ------------------------------------------------

static void test_invalidation_propagation() {
    TEST("invalidate + propagate_invalidation marks dependents stale");
    CompatibilityRegistry reg;
    ProfileId dep = reg.register_profile(ProfileKind::Kernel, kernelPayload("00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100"));
    ProfileId base = reg.register_profile(ProfileKind::Kernel, kernelPayload("00000000-0000-0000-0000-000000000002", "10.0", "f16", "sm_100"), { dep.value });
    CHECK(dep.value != base.value);

    std::vector<Uuid> deps = reg.dependencies_of(base.value);
    bool hasDep = false; for (const auto& u : deps) if (u == dep.value) hasDep = true;
    CHECK(hasDep);
    std::vector<Uuid> dents = reg.dependents_of(dep.value);
    bool hasBase = false; for (const auto& u : dents) if (u == base.value) hasBase = true;
    CHECK(hasBase);

    bool inv = reg.invalidate_profile(dep.value, "obsolete kernel");
    CHECK(inv);
    reg.propagate_invalidation(dep.value);

    CHECK(reg.find_profile(dep.value) == nullptr);   // dependency invalidated
    CHECK(reg.find_profile(base.value) == nullptr);  // dependent marked stale
}

// ---- dependency cycle rejected on register ------------------------------------

static void test_cycle_rejected() {
    TEST("dependency cycle rejected on register");
    CompatibilityRegistry reg;
    ProfileRecord a;
    a.kind = ProfileKind::Kernel;
    a.id = KernelArtifactId::from_string("00000000-0000-0000-0000-000000000001").value;
    Canon::Record ra = Canon::rec(); Canon::put_uint(ra, 1, 1u);
    a.payload = Canon::mk_record(std::move(ra));
    a.deps = {};
    ProfileId aId = reg.register_profile(a);
    CHECK(!aId.is_zero());

    ProfileRecord b;
    b.kind = ProfileKind::Kernel;
    b.id = KernelArtifactId::from_string("00000000-0000-0000-0000-000000000002").value;
    Canon::Record rb = Canon::rec(); Canon::put_uint(rb, 1, 2u);
    b.payload = Canon::mk_record(std::move(rb));
    b.deps = { aId.value };
    ProfileId bId = reg.register_profile(b);
    CHECK(!bId.is_zero());

    // Try to make A depend on B, which forms a cycle A->B->A.
    ProfileRecord a2;
    a2.kind = ProfileKind::Kernel;
    a2.id = aId.value;
    Canon::Record ra2 = Canon::rec(); Canon::put_uint(ra2, 1, 3u);
    a2.payload = Canon::mk_record(std::move(ra2));
    a2.deps = { bId.value };
    bool threw = false;
    try {
        reg.register_profile(a2);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);

    // The supersede path also rejects cycles.
    bool threw2 = false;
    try {
        reg.supersede_profile(ProfileKind::Kernel, aId.value,
                              reg.history_of(aId.value).back().payload, { bId.value });
    } catch (const std::runtime_error&) {
        threw2 = true;
    }
    CHECK(threw2);
}

// ---- matrix deterministic ------------------------------------------------------

static void test_matrix_deterministic() {
    TEST("matrix results deterministic");
    CompatibilityRegistry reg;
    ProfileId k1 = reg.register_profile(ProfileKind::Kernel, kernelPayload("00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100"));
    ProfileId k2 = reg.register_profile(ProfileKind::Kernel, kernelPayload("00000000-0000-0000-0000-000000000002", "12.0", "bf16", "sm_120"));
    ProfileId d1 = reg.register_profile(ProfileKind::Device, devicePayload("00000000-0000-0000-0000-000000000003", "12.0", "sm_120", 32));
    ProfileId d2 = reg.register_profile(ProfileKind::Device, devicePayload("00000000-0000-0000-0000-000000000004", "9.0", "sm_90", 16));
    reg.register_rule(versionRule("00000000-0000-0000-0000-0000000000aa", 0));

    std::vector<Uuid> lefts = { k1.value, k2.value };
    std::vector<Uuid> rights = { d1.value, d2.value };
    MatrixResult m1 = reg.matrix(lefts, rights);
    MatrixResult m2 = reg.matrix(lefts, rights);

    CHECK(m1.cells.size() == 4);
    CHECK(m2.cells.size() == 4);
    CHECK(m1.compatible == m2.compatible);
    CHECK(m1.incompatible == m2.incompatible);
    CHECK(m1.unknown == m2.unknown);
    CHECK(m1.reason_counts.size() == m2.reason_counts.size());
    bool same = true;
    for (std::size_t i = 0; i < m1.cells.size() && i < m2.cells.size(); ++i) {
        if (m1.cells[i].left != m2.cells[i].left ||
            m1.cells[i].right != m2.cells[i].right ||
            m1.cells[i].outcome != m2.cells[i].outcome ||
            m1.cells[i].reason != m2.cells[i].reason) { same = false; }
    }
    CHECK(same);
}

int main() {
    std::printf("start generation"); std::fflush(stdout);
    test_supersede_history();
    test_rule_generation_supersede();
    test_replay();
    test_new_decision_generation();
    test_invalidation_propagation();
    test_cycle_rejected();
    test_matrix_deterministic();
    std::printf(" generation done"); std::fflush(stdout);
    RUN_TESTS();
}
