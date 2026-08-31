#include "testutil.hpp"
#include "compat/compat.hpp"
#include <atomic>
#include <thread>
#include <vector>
using namespace compat;

static bool decode_throws(const std::vector<std::uint8_t>& b) {
    try { canonical_decode(b); return false; } catch (const CanonError&) { return true; }
}

int main() {
    TEST("adversarial canonical decode always throws (never crashes/hangs)");
    // build a valid record then mutate
    Canon::Record r = Canon::rec();
    Canon::put_uint(r, 1, 7); Canon::put_str(r, 2, "x"); Canon::put_bool(r, 3, true);
    std::vector<std::uint8_t> good = canonical_encode(Canon::mk_record(r));
    CHECK(!decode_throws(good));  // valid buffer decodes

    // truncation (every prefix)
    bool allThrow = true;
    for (std::size_t n = 0; n < good.size(); ++n) {
        std::vector<std::uint8_t> t(good.begin(), good.begin() + n);
        if (!decode_throws(t)) allThrow = false;
    }
    CHECK(allThrow);

    // bad magic / wrong version
    std::vector<std::uint8_t> bm = good; bm[0] ^= 0xFF; CHECK(decode_throws(bm));
    std::vector<std::uint8_t> bv = good; bv[4] = 9; CHECK(decode_throws(bv));
    // trailing garbage
    std::vector<std::uint8_t> tg = good; tg.push_back(0xAA); CHECK(decode_throws(tg));
    // truncated length prefix
    std::vector<std::uint8_t> huge = good; huge[9] = 0xFF; CHECK(decode_throws(huge));
    // duplicate tag recorded
    Canon::Record dup = Canon::rec(); Canon::put_uint(dup, 5, 1); Canon::put_uint(dup, 5, 2);
    bool threw = false; try { Canon::mk_record(dup); } catch (const CanonError&) { threw = true; } CHECK(threw);

    TEST("unknown-vs-incompatible semantics");
    CompatibilityRegistry reg;
    TensorProfile t1; t1.schema_id=TensorSchemaId::genseed(); t1.dtype="f32"; t1.layout="A"; t1.rank=2; t1.shape={1,1};
    TensorProfile t2 = t1; t2.schema_id=TensorSchemaId::genseed(); t2.dtype="f16";
    ProfileId a = reg.register_profile(ProfileKind::Tensor, t1.to_canon());
    ProfileId b2 = reg.register_profile(ProfileKind::Tensor, t2.to_canon());
    auto u = reg.evaluate_pair(a, b2);
    CHECK(u.outcome == Outcome::Unknown);            // differing content, no rule -> UNKNOWN (not incompatible)
    CHECK(!(u.outcome == Outcome::Incompatible));    // unknown != incompatible
    // a rule that requires dtype equality fails -> unknown, not incompatible
    CompatibilityRule rr; rr.rule_id=CompatibilityRuleId::genseed(); rr.scope="pair"; rr.outcome=Outcome::Compatible;
    Constraint c; c.op=PredOp::Eq; c.field="dtype"; c.right_field="dtype"; rr.required.push_back(c);
    reg.register_rule(rr);
    CHECK(reg.evaluate_pair(a, b2).outcome == Outcome::Unknown);

    TEST("property: encode/decode roundtrip preserves fingerprint (fixed seed)");
    for (std::uint64_t i = 0; i < 300; ++i) {
        Uuid uid = Uuid::generate_v4(i, i * 7 + 1);
        Canon::Record rec = Canon::rec();
        Canon::put_uuid(rec, 1, uid);
        Canon::put_uint(rec, 2, i);
        Canon::put_str(rec, 3, "payload-" + std::to_string(i));
        Canon::put_bool(rec, 4, (i % 2) == 0);
        Canon cc = Canon::mk_record(rec);
        auto enc = canonical_encode(cc);
        Canon back = canonical_decode(enc);
        CHECK(canonical_fingerprint_hex(cc) == canonical_fingerprint_hex(back));
    }

    TEST("concurrency: concurrent registration + pairwise queries");
    CompatibilityRegistry creg;
    const int T = 8, PER = 60;
    std::vector<std::thread> threads;
    std::atomic<std::size_t> registered{0};
    for (int t = 0; t < T; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < PER; ++i) {
                DeviceProfile d; d.device_id=DeviceId::genseed(); d.compute_capability="12.0";
                d.architecture="sm_120"; d.memory_model="global"; d.total_memory=1ull<<30;
                d.supported_dtypes={"f16"}; d.instruction_classes={"sm_120"};
                creg.register_profile(ProfileKind::Device, d.to_canon());
                registered++;
            }
        });
    }
    for (auto& th : threads) th.join();
    CHECK(registered.load() == static_cast<std::size_t>(T * PER));
    // query concurrently
    std::vector<std::thread> qthreads;
    std::atomic<std::size_t> queried{0};
    auto devs = creg.query_by_kind(ProfileKind::Device);
    Uuid first = devs[0].id;
    for (int t = 0; t < T; ++t) qthreads.emplace_back([&]() {
        for (int i = 0; i < PER*2; ++i) { auto d = creg.evaluate_pair(first, first); if (d.outcome==Outcome::Exact) queried++; }
    });
    for (auto& th : qthreads) th.join();
    CHECK(queried.load() == static_cast<std::size_t>(T * PER * 2));
    CHECK(creg.has_cycle() == false);

    std::printf("concurrent registration produced %zu profiles; queries=%zu\n", registered.load(), queried.load());
    RUN_TESTS();
}
