#include "testutil.hpp"
#include "compat/compat.hpp"
#include <filesystem>
#include <limits>

using namespace compat;

static void test_identity() {
    TEST("uuid roundtrip");
    Uuid u = Uuid::from_string("01234567-89ab-cdef-0123-456789abcdef");
    CHECK(u.to_string() == "01234567-89ab-cdef-0123-456789abcdef");
    Uuid z = Uuid::from_bytes(u.bytes());
    CHECK(z == u);
    Uuid v = Uuid::generate_v4(1, 2);
    CHECK(!v.is_zero());
    Uuid vi = Uuid::from_string(v.to_string());
    CHECK(vi == v);

    TEST("sha256 known vector");
    Sha256::Digest d = Sha256::compute("abc");
    CHECK(Sha256::hex(d) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    TEST("id string roundtrip");
    ModelId m = ModelId::genseed();
    CHECK(ModelId::from_string(m.to_string()) == m);
}

static void test_canonical() {
    TEST("canonical encode/decode + fingerprint determinism");
    Canon::Record r = Canon::rec();
    Canon::put_uint(r, 1, 42);
    Canon::put_str(r, 2, "hello");
    Canon::put_uuid(r, 3, Uuid::from_string("00000000-0000-0000-0000-000000000001"));
    Canon::put_bool(r, 4, true);
    Canon::put_float(r, 5, 3.25);
    Canon c = Canon::mk_record(r);
    std::vector<std::uint8_t> enc = canonical_encode(c);
    Canon d = canonical_decode(enc);
    CHECK(d.kind() == CanonKind::Record);
    CHECK(canonical_fingerprint_hex(c) == canonical_fingerprint_hex(d));
    CHECK(enc.size() >= 6);

    TEST("field order independence");
    Canon::Record a = Canon::rec();
    Canon::put_str(a, 1, "x"); Canon::put_str(a, 2, "y");
    Canon::Record b = Canon::rec();
    Canon::put_str(b, 2, "y"); Canon::put_str(b, 1, "x");
    CHECK(canonical_fingerprint_hex(Canon::mk_record(a)) == canonical_fingerprint_hex(Canon::mk_record(b)));

    TEST("semantic difference changes fingerprint");
    Canon::Record p = Canon::rec(); Canon::put_str(p, 1, "f16");
    Canon::Record q = Canon::rec(); Canon::put_str(q, 1, "bf16");
    CHECK(canonical_fingerprint_hex(Canon::mk_record(p)) != canonical_fingerprint_hex(Canon::mk_record(q)));

    TEST("duplicate tag rejected");
    Canon::Record dup = Canon::rec();
    Canon::put_uint(dup, 1, 1); Canon::put_uint(dup, 1, 2);
    bool threw = false;
    try { Canon::mk_record(dup); } catch (const CanonError&) { threw = true; }
    CHECK(threw);

    TEST("trailing garbage rejected");
    std::vector<std::uint8_t> bad = canonical_encode(c);
    bad.push_back(0xAB);
    threw = false;
    try { canonical_decode(bad); } catch (const CanonError&) { threw = true; }
    CHECK(threw);

    TEST("bad magic rejected");
    std::vector<std::uint8_t> badm = canonical_encode(c);
    badm[0] = 0x00;
    threw = false;
    try { canonical_decode(badm); } catch (const CanonError&) { threw = true; }
    CHECK(threw);

    TEST("NaN rejected");
    threw = false;
    try { Canon::mk_float(std::numeric_limits<double>::quiet_NaN()); } catch (const CanonError&) { threw = true; }
    CHECK(threw);
}

static DeviceProfile make_device(DeviceId id, const std::string& cc) {
    DeviceProfile p;
    p.device_id = id;
    p.vendor = "NVIDIA";
    p.architecture = "sm_" + cc;
    p.compute_capability = cc;
    p.memory_model = "global";
    p.total_memory = 32607ull * 1024ull * 1024ull;
    p.supported_dtypes = {"f16", "f32", "bf16"};
    p.instruction_classes = {"sm_120"};
    return p;
}

static KernelProfile make_kernel(KernelArtifactId id, const std::string& cc) {
    KernelProfile p;
    p.kernel_artifact_id = id;
    p.operation = "add";
    p.architecture = "sm_" + cc;
    p.compute_capability = cc;
    p.abi = "cuda";
    p.runtime = "cudart";
    p.compiler = "nvcc";
    p.compiler_version = "12.9";
    p.dtype = "f32";
    p.layout = "aos";
    p.shape_spec = "[N]";
    return p;
}

static void test_registry() {
    CompatibilityRegistry reg;

    TEST("register device + kernel profiles");
    ModelId model = ModelId::genseed();
    // build a model profile
    ModelProfile mp;
    mp.model_id = model;
    mp.revision_id = ModelRevisionId::genseed();
    mp.architecture = "transformer";
    mp.family = "llm";
    mp.quantization = "fp16";
    mp.tokenizer_id = TokenizerId::genseed();
    mp.vocabulary_id = VocabularyId::genseed();
    mp.dtype = "f16";
    ProfileId mId = reg.register_profile(ProfileKind::Model, mp.to_canon());
    CHECK(!mId.is_zero());

    DeviceId dev120 = DeviceId::genseed();
    DeviceProfile d120 = make_device(dev120, "12.0");
    ProfileId dev120Id = reg.register_profile(ProfileKind::Device, d120.to_canon());
    DeviceId dev90 = DeviceId::genseed();
    DeviceProfile d90 = make_device(dev90, "9.0");
    ProfileId dev90Id = reg.register_profile(ProfileKind::Device, d90.to_canon());

    KernelArtifactId k120 = KernelArtifactId::genseed();
    KernelProfile kk120 = make_kernel(k120, "12.0");
    ProfileId k120Id = reg.register_profile(ProfileKind::Kernel, kk120.to_canon());
    KernelArtifactId k100 = KernelArtifactId::genseed();
    KernelProfile kk100 = make_kernel(k100, "10.0");
    ProfileId k100Id = reg.register_profile(ProfileKind::Kernel, kk100.to_canon());

    TEST("exact identity -> EXACT");
    CompatibilityDecision d = reg.evaluate_pair(dev120Id, dev120Id);
    CHECK(d.outcome == Outcome::Exact);

    TEST("register kernel/device compatibility rule");
    CompatibilityRule rule;
    rule.rule_id = CompatibilityRuleId::genseed();
    rule.domain = "kernel-device";
    rule.scope = "pair";
    rule.left_kind = ProfileKind::Kernel;
    rule.right_kind = ProfileKind::Device;
    rule.kind = RuleKind::Equality;
    rule.outcome = Outcome::Compatible;
    Constraint req; req.op = PredOp::VersionLe; req.field = "compute_capability"; req.right_field = "compute_capability";
    rule.required.push_back(req);
    Constraint inc; inc.op = PredOp::VersionGt; inc.field = "compute_capability"; inc.right_field = "compute_capability";
    rule.incompatible_with.push_back(inc);
    reg.register_rule(rule);

    TEST("kernel 12.0 on device 12.0 -> COMPATIBLE");
    d = reg.evaluate_pair(k120Id, dev120Id);
    CHECK(d.outcome == Outcome::Compatible);

    TEST("kernel 10.0 on device 12.0 -> COMPATIBLE (caps down)");
    d = reg.evaluate_pair(k100Id, dev120Id);
    CHECK(d.outcome == Outcome::Compatible);

    TEST("kernel 12.0 on device 9.0 -> INCOMPATIBLE (before execution)");
    d = reg.evaluate_pair(k120Id, dev90Id);
    CHECK(d.outcome == Outcome::Incompatible);
    CHECK(!d.failed.empty());

    TEST("generation supersede produces a new decision and keeps history");
    CompatibilityDecision before = reg.evaluate_pair(k120Id, dev120Id);
    // rebuild kernel with new generation (same id, same content) -> no change
    ProfileId k120Id2 = reg.supersede_profile(ProfileKind::Kernel, k120Id.value, kk120.to_canon());
    CHECK(k120Id2.to_string() == k120Id.to_string());
    CHECK(before.outcome == Outcome::Compatible);

    TEST("matrix produces deterministic counts");
    MatrixResult mr = reg.matrix({k120Id, k100Id}, {dev120Id, dev90Id});
    CHECK(mr.cells.size() == 4);
    CHECK(mr.compatible == 2);  // k120/dev120, k100/dev120
    CHECK(mr.incompatible == 2); // k120/dev90, k100/dev90

    TEST("snapshot / recover preserves fingerprint and decision replay");
    CompatibilityDecision dref = reg.evaluate_pair(k120Id, dev120Id);
    std::vector<std::uint8_t> snap = reg.snapshot();
    auto reg2 = CompatibilityRegistry::recover(snap);
    CHECK(reg2 != nullptr);
    CompatibilityDecision d2 = reg2->evaluate_pair(k120Id, dev120Id);
    CHECK(d2.outcome == Outcome::Compatible);
    CHECK(d2.digest == dref.digest);
    CHECK(d2.decision_id == dref.decision_id);

    TEST("load/save roundtrip");
    const std::string path = (std::filesystem::temp_directory_path() / "compat_test_registry.bin").string();
    CHECK(reg.save(path));
    auto reg3 = CompatibilityRegistry::load(path);
    CHECK(reg3 != nullptr);
    CHECK(reg3->evaluate_pair(k120Id, dev120Id).outcome == Outcome::Compatible);

    TEST("corruption rejected on load");
    std::vector<std::uint8_t> corrupt = snap;
    corrupt[corrupt.size() - 1] ^= 0xFF;
    bool threw = false;
    try { CompatibilityRegistry::recover(corrupt); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);

    TEST("unknown remains unknown without evidence");
    // A model vs kernel pair with no rule -> UNKNOWN
    CompatibilityDecision unk = reg.evaluate_pair(mId, k120Id);
    CHECK(unk.outcome == Outcome::Unknown);
}

int main() {
    std::printf("start\n"); std::fflush(stdout);
    test_identity();
    std::printf("identity done\n"); std::fflush(stdout);
    test_canonical();
    std::printf("canonical done\n"); std::fflush(stdout);
    test_registry();
    std::printf("registry done\n"); std::fflush(stdout);
    RUN_TESTS();
}
