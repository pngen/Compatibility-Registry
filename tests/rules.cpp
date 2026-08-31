// rules.cpp -- rule-resolution proof.
// Exercises each predicate / rule-kind and the deterministic resolution order:
// priority desc, then rule_id asc, with explicit conflict surfacing.
#include "testutil.hpp"
#include "compat/compat.hpp"
#include <cstdint>
#include <string>
#include <vector>

using namespace compat;

// ---- profile builders -----------------------------------------------------

static ProfileId addKernel(CompatibilityRegistry& reg, const std::string& idHex,
                           const std::string& cc, const std::string& dtype,
                           const std::string& arch, const std::string& operation = "add") {
    KernelProfile p;
    p.kernel_artifact_id = KernelArtifactId::from_string(idHex);
    p.operation = operation;
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
    return reg.register_profile(ProfileKind::Kernel, p.to_canon());
}

static ProfileId addDevice(CompatibilityRegistry& reg, const std::string& idHex,
                           const std::string& cc, const std::string& arch, std::uint64_t memGB) {
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
    return reg.register_profile(ProfileKind::Device, p.to_canon());
}

static ProfileId addModel(CompatibilityRegistry& reg, const std::string& idHex, bool feature) {
    ModelProfile mp;
    mp.model_id = ModelId::from_string(idHex);
    mp.revision_id = ModelRevisionId::from_string("00000000-0000-0000-0000-0000000000b1");
    mp.architecture = "transformer";
    mp.family = "llm";
    mp.quantization = "fp16";
    mp.tokenizer_id = TokenizerId::from_string("00000000-0000-0000-0000-0000000000c1");
    mp.vocabulary_id = VocabularyId::from_string("00000000-0000-0000-0000-0000000000d1");
    mp.dtype = "f16";
    Canon::Record ex = Canon::rec();
    Canon::put_bool(ex, 1, feature);   // capability subfield tag 1 = "compute_capability"
    mp.execution_req = ex;
    return reg.register_profile(ProfileKind::Model, mp.to_canon());
}

// ---- rule builders --------------------------------------------------------

static CompatibilityRule baseRule(const std::string& idHex, const char* scope,
                                  ProfileKind lk, ProfileKind rk, RuleKind kind,
                                  Outcome out, int prio) {
    CompatibilityRule rule;
    rule.rule_id = CompatibilityRuleId::from_string(idHex);
    rule.scope = scope;
    rule.left_kind = lk;
    rule.right_kind = rk;
    rule.kind = kind;
    rule.outcome = out;
    rule.priority = prio;
    rule.active = true;
    return rule;
}

static void addReq(CompatibilityRule& rule, PredOp op, const std::string& field,
                   const Canon& expected, const std::string& right_field = "") {
    Constraint c;
    c.op = op;
    c.field = field;
    c.expected = expected;
    c.right_field = right_field;
    rule.required.push_back(c);
}

static void addInSet(CompatibilityRule& rule, const std::string& field, const std::vector<Canon>& set) {
    Constraint c;
    c.op = PredOp::InSet;
    c.field = field;
    c.set = set;
    rule.required.push_back(c);
}

static void addIncompat(CompatibilityRule& rule, PredOp op, const std::string& field,
                        const Canon& expected, const std::string& right_field) {
    Constraint c;
    c.op = op;
    c.field = field;
    c.expected = expected;
    c.right_field = right_field;
    rule.incompatible_with.push_back(c);
}

static bool provenanceHasConflict(const CompatibilityDecision& d) {
    for (const auto& p : d.provenance) {
        if (p.rfind("conflict:", 0) == 0) return true;
    }
    return false;
}

// ---- equality -------------------------------------------------------------

static void test_equality() {
    TEST("equality: matching field -> compatible, differing -> unknown");
    CompatibilityRegistry reg;
    ProfileId k1 = addKernel(reg, "00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100");
    ProfileId k2 = addKernel(reg, "00000000-0000-0000-0000-000000000002", "12.0", "bf16", "sm_120");
    ProfileId d1 = addDevice(reg, "00000000-0000-0000-0000-000000000003", "12.0", "sm_120", 32);
    CompatibilityRule rule = baseRule("00000000-0000-0000-0000-0000000000aa", "pair",
                                      ProfileKind::Kernel, ProfileKind::Device, RuleKind::Equality, Outcome::Compatible, 0);
    addReq(rule, PredOp::Eq, "dtype", Canon::mk_str("f16"));
    reg.register_rule(rule);
    CHECK(reg.evaluate_pair(k1.value, d1.value).outcome == Outcome::Compatible);
    CHECK(reg.evaluate_pair(k2.value, d1.value).outcome == Outcome::Unknown);
}

// ---- set membership -------------------------------------------------------

static void test_set_membership() {
    TEST("set membership: value in allowed set -> compatible, otherwise unknown");
    CompatibilityRegistry reg;
    ProfileId k1 = addKernel(reg, "00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100");
    ProfileId k2 = addKernel(reg, "00000000-0000-0000-0000-000000000002", "12.0", "bf16", "sm_120");
    ProfileId k3 = addKernel(reg, "00000000-0000-0000-0000-000000000003", "11.0", "f32", "sm_110");
    ProfileId d1 = addDevice(reg, "00000000-0000-0000-0000-000000000004", "12.0", "sm_120", 32);
    CompatibilityRule rule = baseRule("00000000-0000-0000-0000-0000000000aa", "pair",
                                      ProfileKind::Kernel, ProfileKind::Device, RuleKind::Equality, Outcome::Compatible, 0);
    std::vector<Canon> set;
    set.push_back(Canon::mk_str("f16"));
    set.push_back(Canon::mk_str("bf16"));
    addInSet(rule, "dtype", set);
    reg.register_rule(rule);
    CHECK(reg.evaluate_pair(k1.value, d1.value).outcome == Outcome::Compatible);
    CHECK(reg.evaluate_pair(k2.value, d1.value).outcome == Outcome::Compatible);
    CHECK(reg.evaluate_pair(k3.value, d1.value).outcome == Outcome::Unknown);
}

// ---- numeric range (min / max) --------------------------------------------

static void test_numeric_range() {
    TEST("numeric range: min+max conjunction, device is left");
    CompatibilityRegistry reg;
    ProfileId d1 = addDevice(reg, "00000000-0000-0000-0000-000000000001", "12.0", "sm_120", 32);
    ProfileId d2 = addDevice(reg, "00000000-0000-0000-0000-000000000002", "9.0", "sm_90", 16);
    ProfileId k1 = addKernel(reg, "00000000-0000-0000-0000-000000000003", "10.0", "f16", "sm_100");
    CompatibilityRule rule = baseRule("00000000-0000-0000-0000-0000000000aa", "pair",
                                      ProfileKind::Device, ProfileKind::Kernel, RuleKind::Equality, Outcome::Compatible, 0);
    std::uint64_t minV = 24ull * 1024ull * 1024ull * 1024ull;
    std::uint64_t maxV = 48ull * 1024ull * 1024ull * 1024ull;
    addReq(rule, PredOp::Min, "total_memory", Canon::mk_uint(minV));   // >= 24GB
    addReq(rule, PredOp::Max, "total_memory", Canon::mk_uint(maxV));   // <= 48GB
    reg.register_rule(rule);
    CHECK(reg.evaluate_pair(d1.value, k1.value).outcome == Outcome::Compatible);
    CHECK(reg.evaluate_pair(d2.value, k1.value).outcome == Outcome::Unknown);

    // Max alone: a device above the cap is rejected.
    CompatibilityRegistry reg2;
    ProfileId d3 = addDevice(reg2, "00000000-0000-0000-0000-000000000001", "12.0", "sm_120", 32);
    ProfileId d4 = addDevice(reg2, "00000000-0000-0000-0000-000000000002", "10.0", "sm_100", 16);
    ProfileId k2 = addKernel(reg2, "00000000-0000-0000-0000-000000000003", "10.0", "f16", "sm_100");
    CompatibilityRule rule2 = baseRule("00000000-0000-0000-0000-0000000000aa", "pair",
                                       ProfileKind::Device, ProfileKind::Kernel, RuleKind::Equality, Outcome::Compatible, 0);
    std::uint64_t maxCap = 20ull * 1024ull * 1024ull * 1024ull;
    addReq(rule2, PredOp::Max, "total_memory", Canon::mk_uint(maxCap));  // cap 20GB
    reg2.register_rule(rule2);
    CHECK(reg2.evaluate_pair(d3.value, k2.value).outcome == Outcome::Unknown);  // 32GB > 20GB cap
    CHECK(reg2.evaluate_pair(d4.value, k2.value).outcome == Outcome::Compatible); // 16GB <= 20GB
}

// ---- version relation -------------------------------------------------------

static void test_version_relation() {
    TEST("version relation: kernel cc <= device cc");
    CompatibilityRegistry reg;
    ProfileId k1 = addKernel(reg, "00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100");
    ProfileId k2 = addKernel(reg, "00000000-0000-0000-0000-000000000002", "12.0", "bf16", "sm_120");
    ProfileId d1 = addDevice(reg, "00000000-0000-0000-0000-000000000003", "12.0", "sm_120", 32);
    ProfileId d2 = addDevice(reg, "00000000-0000-0000-0000-000000000004", "9.0", "sm_90", 16);
    CompatibilityRule rule = baseRule("00000000-0000-0000-0000-0000000000aa", "pair",
                                      ProfileKind::Kernel, ProfileKind::Device, RuleKind::Equality, Outcome::Compatible, 0);
    addReq(rule, PredOp::VersionLe, "compute_capability", Canon(), "compute_capability");
    reg.register_rule(rule);
    CHECK(reg.evaluate_pair(k1.value, d1.value).outcome == Outcome::Compatible);  // 10 <= 12
    CHECK(reg.evaluate_pair(k2.value, d2.value).outcome == Outcome::Unknown);     // 12 > 9

    // VersionGt in the other direction.
    CompatibilityRegistry reg2;
    ProfileId kA = addKernel(reg2, "00000000-0000-0000-0000-000000000001", "12.0", "f16", "sm_120");
    ProfileId dB = addDevice(reg2, "00000000-0000-0000-0000-000000000002", "9.0", "sm_90", 16);
    CompatibilityRule rule2 = baseRule("00000000-0000-0000-0000-0000000000aa", "pair",
                                       ProfileKind::Kernel, ProfileKind::Device, RuleKind::Equality, Outcome::Compatible, 0);
    addReq(rule2, PredOp::VersionGt, "compute_capability", Canon(), "compute_capability");
    reg2.register_rule(rule2);
    CHECK(reg2.evaluate_pair(kA.value, dB.value).outcome == Outcome::Compatible); // 12 > 9
}

// ---- arch family ------------------------------------------------------------

static void test_arch_family() {
    TEST("arch family: prefix of an architecture string");
    CompatibilityRegistry reg;
    ProfileId k1 = addKernel(reg, "00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100");
    ProfileId k2 = addKernel(reg, "00000000-0000-0000-0000-000000000002", "10.0", "f16", "arm64");
    ProfileId d1 = addDevice(reg, "00000000-0000-0000-0000-000000000003", "12.0", "sm_120", 32);
    CompatibilityRule rule = baseRule("00000000-0000-0000-0000-0000000000aa", "pair",
                                      ProfileKind::Kernel, ProfileKind::Device, RuleKind::Equality, Outcome::Compatible, 0);
    Constraint c;
    c.op = PredOp::ArchFamily;
    c.field = "architecture";
    c.set.push_back(Canon::mk_str("sm_"));
    rule.required.push_back(c);
    reg.register_rule(rule);
    CHECK(reg.evaluate_pair(k1.value, d1.value).outcome == Outcome::Compatible);  // sm_100 in "sm_"
    CHECK(reg.evaluate_pair(k2.value, d1.value).outcome == Outcome::Unknown);     // arm64 not in "sm_"
}

// ---- feature requirement ------------------------------------------------------

static void test_feature_requirement() {
    TEST("feature requirement: bool flag must be observed/true");
    CompatibilityRegistry reg;
    ProfileId mFlag = addModel(reg, "00000000-0000-0000-0000-000000000001", true);
    ProfileId mNo = addModel(reg, "00000000-0000-0000-0000-000000000002", false);
    CompatibilityRule rule = baseRule("00000000-0000-0000-0000-0000000000aa", "pair",
                                      ProfileKind::Model, ProfileKind::Model, RuleKind::Equality, Outcome::Compatible, 0);
    addReq(rule, PredOp::FeatureReq, "execution_req.compute_capability", Canon());
    reg.register_rule(rule);
    CHECK(reg.evaluate_pair(mFlag.value, mNo.value).outcome == Outcome::Compatible);
    CHECK(reg.evaluate_pair(mNo.value, mFlag.value).outcome == Outcome::Unknown);
}

// ---- conjunction (requires) ----------------------------------------------------

static void test_conjunction() {
    TEST("conjunction: every required constraint must hold");
    CompatibilityRegistry reg;
    ProfileId k1 = addKernel(reg, "00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100");
    ProfileId k2 = addKernel(reg, "00000000-0000-0000-0000-000000000002", "12.0", "bf16", "sm_120");
    ProfileId d1 = addDevice(reg, "00000000-0000-0000-0000-000000000003", "12.0", "sm_120", 32);
    CompatibilityRule rule = baseRule("00000000-0000-0000-0000-0000000000aa", "pair",
                                      ProfileKind::Kernel, ProfileKind::Device, RuleKind::Equality, Outcome::Compatible, 90);
    addReq(rule, PredOp::Eq, "dtype", Canon::mk_str("f16"));
    addReq(rule, PredOp::VersionLe, "compute_capability", Canon(), "compute_capability");
    reg.register_rule(rule);
    CHECK(reg.evaluate_pair(k1.value, d1.value).outcome == Outcome::Compatible);  // both hold
    CHECK(reg.evaluate_pair(k2.value, d1.value).outcome == Outcome::Unknown);     // dtype fails
}

// ---- disjunction (any_of) -------------------------------------------------------

static void test_disjunction() {
    TEST("disjunction: any one of the allowed alternatives suffices");
    CompatibilityRegistry reg;
    ProfileId k1 = addKernel(reg, "00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100", "add");
    ProfileId kSub = addKernel(reg, "00000000-0000-0000-0000-000000000002", "11.0", "f32", "sm_110", "sub");
    ProfileId kMul = addKernel(reg, "00000000-0000-0000-0000-000000000003", "11.0", "f32", "sm_110", "mul");
    ProfileId d1 = addDevice(reg, "00000000-0000-0000-0000-000000000004", "12.0", "sm_120", 32);
    CompatibilityRule rule = baseRule("00000000-0000-0000-0000-0000000000aa", "pair",
                                      ProfileKind::Kernel, ProfileKind::Device, RuleKind::Equality, Outcome::Compatible, 0);
    Constraint a = {}; a.op = PredOp::Eq; a.field = "dtype"; a.expected = Canon::mk_str("f16");
    Constraint b = {}; b.op = PredOp::Eq; b.field = "operation"; b.expected = Canon::mk_str("sub");
    rule.any_of.push_back(a);
    rule.any_of.push_back(b);
    reg.register_rule(rule);
    CHECK(reg.evaluate_pair(k1.value, d1.value).outcome == Outcome::Compatible);    // dtype f16
    CHECK(reg.evaluate_pair(kSub.value, d1.value).outcome == Outcome::Compatible);  // operation sub
    CHECK(reg.evaluate_pair(kMul.value, d1.value).outcome == Outcome::Unknown);     // neither
}

// ---- explicit incompatibility --------------------------------------------------

static void test_explicit_incompat() {
    TEST("incompatible_with is a defensive veto");
    CompatibilityRegistry reg;
    ProfileId k1 = addKernel(reg, "00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100");
    ProfileId k2 = addKernel(reg, "00000000-0000-0000-0000-000000000002", "12.0", "bf16", "sm_120");
    ProfileId d1 = addDevice(reg, "00000000-0000-0000-0000-000000000003", "12.0", "sm_120", 32);
    ProfileId d2 = addDevice(reg, "00000000-0000-0000-0000-000000000004", "9.0", "sm_90", 16);
    CompatibilityRule rule = baseRule("00000000-0000-0000-0000-0000000000aa", "pair",
                                      ProfileKind::Kernel, ProfileKind::Device, RuleKind::Equality, Outcome::Compatible, 0);
    addIncompat(rule, PredOp::VersionGt, "compute_capability", Canon(), "compute_capability");
    reg.register_rule(rule);
    CompatibilityDecision good = reg.evaluate_pair(k1.value, d1.value);   // 10 <= 12, no veto
    CHECK(good.outcome == Outcome::Compatible);
    CompatibilityDecision bad = reg.evaluate_pair(k2.value, d2.value);    // 12 > 9, veto fired
    CHECK(bad.outcome == Outcome::Incompatible);
    CHECK(!bad.failed.empty());

    // RuleKind::ExplicitIncompat always yields incompatible for a matching pair.
    CompatibilityRegistry reg2;
    ProfileId kA = addKernel(reg2, "00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100");
    ProfileId dB = addDevice(reg2, "00000000-0000-0000-0000-000000000002", "12.0", "sm_120", 32);
    CompatibilityRule r2 = baseRule("00000000-0000-0000-0000-0000000000aa", "pair",
                                    ProfileKind::Kernel, ProfileKind::Device, RuleKind::ExplicitIncompat, Outcome::Compatible, 0);
    reg2.register_rule(r2);
    CHECK(reg2.evaluate_pair(kA.value, dB.value).outcome == Outcome::Incompatible);
}

// ---- conditional ----------------------------------------------------------------

static void test_conditional() {
    TEST("conditional: condition holds -> rule outcome, else CONDITIONAL");
    CompatibilityRegistry reg;
    ProfileId k1 = addKernel(reg, "00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100");
    ProfileId k2 = addKernel(reg, "00000000-0000-0000-0000-000000000002", "12.0", "bf16", "sm_120");
    ProfileId d1 = addDevice(reg, "00000000-0000-0000-0000-000000000003", "12.0", "sm_120", 32);
    CompatibilityRule rule = baseRule("00000000-0000-0000-0000-0000000000aa", "pair",
                                      ProfileKind::Kernel, ProfileKind::Device, RuleKind::Conditional, Outcome::Compatible, 0);
    Constraint cond;
    cond.op = PredOp::Eq;
    cond.field = "dtype";
    cond.expected = Canon::mk_str("f16");
    rule.conditions.push_back(cond);
    reg.register_rule(rule);
    CHECK(reg.evaluate_pair(k1.value, d1.value).outcome == Outcome::Compatible);
    CHECK(reg.evaluate_pair(k2.value, d1.value).outcome == Outcome::Conditional);
}

// ---- adaptation ----------------------------------------------------------------

static void test_adaptation() {
    TEST("adaptation: produces compatible-with-adaptation and lists adaptations");
    CompatibilityRegistry reg;
    ProfileId k1 = addKernel(reg, "00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100");
    ProfileId d1 = addDevice(reg, "00000000-0000-0000-0000-000000000002", "12.0", "sm_120", 32);
    CompatibilityRule rule = baseRule("00000000-0000-0000-0000-0000000000aa", "pair",
                                      ProfileKind::Kernel, ProfileKind::Device, RuleKind::Adaptation, Outcome::Compatible, 0);
    rule.adaptations.push_back("layout_permute");
    rule.adaptations.push_back("dtype_cast_bf16");
    reg.register_rule(rule);
    CompatibilityDecision d = reg.evaluate_pair(k1.value, d1.value);
    CHECK(d.outcome == Outcome::CompatibleWithAdaptation);
    CHECK(d.adaptations.size() == 2);
    CHECK(d.adaptations[0] == "layout_permute");
    CHECK(d.adaptations[1] == "dtype_cast_bf16");
}

// ---- deterministic tie-break + conflict -----------------------------------------

static void test_tiebreak() {
    TEST("tie-break: same priority, lower rule_id wins, conflict surfaced");
    CompatibilityRegistry reg;
    ProfileId k1 = addKernel(reg, "00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100");
    ProfileId d1 = addDevice(reg, "00000000-0000-0000-0000-000000000002", "12.0", "sm_120", 32);
    CompatibilityRule ra = baseRule("00000000-0000-0000-0000-0000000000a1", "pair",
                                    ProfileKind::Kernel, ProfileKind::Device, RuleKind::Equality, Outcome::Compatible, 5);
    addReq(ra, PredOp::Eq, "dtype", Canon::mk_str("f16"));
    CompatibilityRule rb = baseRule("00000000-0000-0000-0000-0000000000a2", "pair",
                                    ProfileKind::Kernel, ProfileKind::Device, RuleKind::Adaptation, Outcome::Compatible, 5);
    rb.adaptations.push_back("x");
    reg.register_rule(ra);
    reg.register_rule(rb);
    CompatibilityDecision d = reg.evaluate_pair(k1.value, d1.value);
    CHECK(d.outcome == Outcome::Compatible);          // a1 < a2, so rule 1 (Compatible) wins
    CHECK(provenanceHasConflict(d));                  // conflict surfaced vs rule 2

    TEST("tie-break: higher priority wins over rule_id order");
    CompatibilityRegistry reg2;
    ProfileId kA = addKernel(reg2, "00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100");
    ProfileId dB = addDevice(reg2, "00000000-0000-0000-0000-000000000002", "12.0", "sm_120", 32);
    CompatibilityRule r1 = baseRule("00000000-0000-0000-0000-000000000011", "pair",
                                    ProfileKind::Kernel, ProfileKind::Device, RuleKind::Equality, Outcome::Compatible, 10);
    addReq(r1, PredOp::Eq, "dtype", Canon::mk_str("f16"));
    CompatibilityRule r2 = baseRule("00000000-0000-0000-0000-0000000000a1", "pair",
                                    ProfileKind::Kernel, ProfileKind::Device, RuleKind::Adaptation, Outcome::Compatible, 20);
    r2.adaptations.push_back("y");
    reg2.register_rule(r1);
    reg2.register_rule(r2);
    CompatibilityDecision d2 = reg2.evaluate_pair(kA.value, dB.value);
    CHECK(d2.outcome == Outcome::CompatibleWithAdaptation);  // priority 20 wins
    CHECK(provenanceHasConflict(d2));
}

// ---- unknown semantics ------------------------------------------------------------

static void test_unknown_semantics() {
    TEST("unknown stays unknown without new evidence; unknown != incompatible");
    CompatibilityRegistry reg;
    ProfileId k1 = addKernel(reg, "00000000-0000-0000-0000-000000000001", "10.0", "f16", "sm_100");
    ProfileId d1 = addDevice(reg, "00000000-0000-0000-0000-000000000002", "12.0", "sm_120", 32);
    CompatibilityDecision a = reg.evaluate_pair(k1.value, d1.value);
    CHECK(a.outcome == Outcome::Unknown);
    CHECK(a.outcome != Outcome::Incompatible);
    CompatibilityDecision b = reg.evaluate_pair(k1.value, d1.value);
    CHECK(b.outcome == Outcome::Unknown);   // still unknown, no new evidence added
    CHECK(a.outcome == b.outcome);
}

int main() {
    std::printf("start rules"); std::fflush(stdout);
    test_equality();
    test_set_membership();
    test_numeric_range();
    test_version_relation();
    test_arch_family();
    test_feature_requirement();
    test_conjunction();
    test_disjunction();
    test_explicit_incompat();
    test_conditional();
    test_adaptation();
    test_tiebreak();
    test_unknown_semantics();
    std::printf("rules done"); std::fflush(stdout);
    RUN_TESTS();
}
