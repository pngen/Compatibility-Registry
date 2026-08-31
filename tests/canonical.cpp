// canonical.cpp -- canonical-identity proof.
// Demonstrates that the canonical encoding is a strict, deterministic identity:
// identical content -> identical fingerprint, field order is irrelevant, and any
// semantic difference or malformed input is rejected (never silently accepted).
#include "testutil.hpp"
#include "compat/compat.hpp"
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace compat;

// Build a simple model-shaped record (all fields inserted in "natural" order).
static Canon make_model_payload(const std::string& dtype) {
    ModelProfile mp;
    mp.model_id = ModelId::from_string("00000000-0000-0000-0000-0000000000a1");
    mp.revision_id = ModelRevisionId::from_string("00000000-0000-0000-0000-0000000000b2");
    mp.architecture = "transformer";
    mp.family = "llm";
    mp.quantization = "fp16";
    mp.tokenizer_id = TokenizerId::from_string("00000000-0000-0000-0000-0000000000c3");
    mp.vocabulary_id = VocabularyId::from_string("00000000-0000-0000-0000-0000000000d4");
    mp.dtype = dtype;
    return mp.to_canon();
}

// Push one byte.
static void push8(std::vector<std::uint8_t>& v, std::uint8_t b) { v.push_back(b); }
// Push a big-endian u32.
static void pushU32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    push8(v, static_cast<std::uint8_t>((x >> 24) & 0xFFu));
    push8(v, static_cast<std::uint8_t>((x >> 16) & 0xFFu));
    push8(v, static_cast<std::uint8_t>((x >> 8) & 0xFFu));
    push8(v, static_cast<std::uint8_t>(x & 0xFFu));
}
// Prefix of a valid canonical container: magic 'CRCF' + version 1.
static void pushHeader(std::vector<std::uint8_t>& v) {
    push8(v, 0x43u); push8(v, 0x52u); push8(v, 0x43u); push8(v, 0x46u);
    push8(v, 0x01u);
}

// Build a valid encoded record {tag=1 -> uint(42)}.
static std::vector<std::uint8_t> simpleUintRecord() {
    Canon::Record r = Canon::rec();
    Canon::put_uint(r, 1, 42);
    return canonical_encode(Canon::mk_record(std::move(r)));
}

// A record whose body has no length; used for crafted-buffer rejection tests.
static void pushUintBody(std::vector<std::uint8_t>& v, std::uint32_t tag, std::uint64_t value) {
    pushU32(v, tag);
    push8(v, 0x01u);            // Uint kind
    pushU32(v, 8u);             // length == 8
    for (int i = 7; i >= 0; --i) push8(v, static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu));
}

static bool decodeThrows(std::vector<std::uint8_t> bytes) {
    bool threw = false;
    try {
        canonical_decode(bytes);
    } catch (const CanonError&) {
        threw = true;
    }
    return threw;
}

static bool floatThrows(double v) {
    bool threw = false;
    try {
        (void)Canon::mk_float(v);
    } catch (const CanonError&) {
        threw = true;
    }
    return threw;
}

static void test_identical_fingerprint() {
    TEST("identical profiles -> identical fingerprint");
    Canon a = make_model_payload("f16");
    Canon b = make_model_payload("f16");
    CHECK(canonical_fingerprint_hex(a) == canonical_fingerprint_hex(b));

    Canon::Record r = Canon::rec();
    Canon::put_str(r, modelf::Dtype, "f16");
    Canon c = Canon::mk_record(std::move(r));
    CHECK(canonical_fingerprint_hex(c) == canonical_fingerprint_hex(c));
}

static void test_field_order_independence() {
    TEST("field order independence");
    Canon::Record a = Canon::rec();
    Canon::put_str(a, 1, "x");
    Canon::put_str(a, 2, "y");
    Canon::put_uint(a, 3, 7);
    Canon::Record b = Canon::rec();
    Canon::put_uint(b, 3, 7);
    Canon::put_str(b, 2, "y");
    Canon::put_str(b, 1, "x");
    CHECK(canonical_fingerprint_hex(Canon::mk_record(a)) == canonical_fingerprint_hex(Canon::mk_record(b)));

    // Nested record order independence.
    Canon::Record inner1 = Canon::rec(); Canon::put_str(inner1, 1, "p"); Canon::put_str(inner1, 2, "q");
    Canon::Record inner2 = Canon::rec(); Canon::put_str(inner2, 2, "q"); Canon::put_str(inner2, 1, "p");
    Canon::Record outer1 = Canon::rec(); Canon::put_record(outer1, 9, std::move(inner1)); Canon::put_uint(outer1, 1, 5);
    Canon::Record outer2 = Canon::rec(); Canon::put_uint(outer2, 1, 5); Canon::put_record(outer2, 9, std::move(inner2));
    CHECK(canonical_fingerprint_hex(Canon::mk_record(outer1)) == canonical_fingerprint_hex(Canon::mk_record(outer2)));
}

static void test_semantic_difference() {
    TEST("semantically different profiles -> different fingerprints");
    CHECK(canonical_fingerprint_hex(make_model_payload("f16")) != canonical_fingerprint_hex(make_model_payload("bf16")));

    Canon::Record p = Canon::rec(); Canon::put_uint(p, 1, 1);
    Canon::Record q = Canon::rec(); Canon::put_uint(q, 1, 2);
    CHECK(canonical_fingerprint_hex(Canon::mk_record(p)) != canonical_fingerprint_hex(Canon::mk_record(q)));

    // bool false vs absent differ.
    Canon::Record withFalse = Canon::rec(); Canon::put_bool(withFalse, 1, false);
    CHECK(canonical_fingerprint_hex(Canon::mk_record(withFalse)) != canonical_fingerprint_hex(Canon::mk_record(Canon::rec())));
}

static void test_identity_exact() {
    TEST("exact identity implies EXACT");
    CompatibilityRegistry reg;
    Canon p = make_model_payload("f16");
    ProfileId a = reg.register_profile(ProfileKind::Model, p);
    ProfileId b = reg.register_profile(ProfileKind::Model, p);
    CHECK(!a.is_zero());
    CHECK(!b.is_zero());
    CHECK(a.to_string() != b.to_string());
    CompatibilityDecision d = reg.evaluate_pair(a.value, b.value);
    CHECK(d.outcome == Outcome::Exact);
    CHECK(d.rule_id.is_zero());
}

static void test_malformed_rejected() {
    TEST("trailing garbage rejected");
    std::vector<std::uint8_t> g = simpleUintRecord();
    g.push_back(0xABu);
    CHECK(decodeThrows(g));

    TEST("bad magic rejected");
    std::vector<std::uint8_t> m = simpleUintRecord();
    m[0] = 0x00u;
    CHECK(decodeThrows(m));

    TEST("unsupported version rejected");
    std::vector<std::uint8_t> ver = simpleUintRecord();
    ver[4] = 0x09u;
    CHECK(decodeThrows(ver));

    TEST("duplicate tag rejected at construction");
    Canon::Record dup = Canon::rec();
    Canon::put_uint(dup, 1, 1);
    Canon::put_uint(dup, 1, 2);
    bool threw = false;
    try { (void)Canon::mk_record(dup); } catch (const CanonError&) { threw = true; }
    CHECK(threw);

    TEST("duplicate tag rejected at decode");
    std::vector<std::uint8_t> dupBytes;
    pushHeader(dupBytes);
    push8(dupBytes, 0x02u);            // Record kind
    std::uint32_t bodyLen = static_cast<std::uint32_t>(2 * (4 + 1 + 4 + 8));
    pushU32(dupBytes, bodyLen);
    pushUintBody(dupBytes, 1u, 0x2Au);
    pushUintBody(dupBytes, 1u, 0x2Au);
    CHECK(decodeThrows(dupBytes));

    TEST("malformed length rejected (child length exceeds segment)");
    std::vector<std::uint8_t> len = simpleUintRecord();
    len[6] = 0xFFu; len[7] = 0xFFu; len[8] = 0xFFu; len[9] = 0xFFu;
    CHECK(decodeThrows(len));

    TEST("truncated buffer rejected");
    std::vector<std::uint8_t> trunc = simpleUintRecord();
    trunc.resize(trunc.size() - 1);
    CHECK(decodeThrows(trunc));

    TEST("buffer too small rejected");
    std::vector<std::uint8_t> small = { 0x43u, 0x52u, 0x43u };
    CHECK(decodeThrows(small));

    TEST("invalid bool value rejected at decode");
    std::vector<std::uint8_t> badBool;
    pushHeader(badBool);
    push8(badBool, 0x03u);      // Bool kind
    pushU32(badBool, 1u);
    push8(badBool, 0x07u);
    CHECK(decodeThrows(badBool));

    TEST("unknown kind rejected at decode");
    std::vector<std::uint8_t> badKind;
    pushHeader(badKind);
    push8(badKind, 0x7Fu);
    pushU32(badKind, 0u);
    CHECK(decodeThrows(badKind));
}

static void test_nan_inf_rejected() {
    TEST("NaN rejected at construction");
    CHECK(floatThrows(std::numeric_limits<double>::quiet_NaN()));
    CHECK(floatThrows(-std::numeric_limits<double>::quiet_NaN()));

    TEST("Inf rejected at construction");
    CHECK(floatThrows(std::numeric_limits<double>::infinity()));
    CHECK(floatThrows(-std::numeric_limits<double>::infinity()));

    TEST("NaN/Inf rejected at decode");
    std::vector<std::uint8_t> posInf;
    pushHeader(posInf);
    push8(posInf, 0x07u);         // Float kind
    pushU32(posInf, 8u);
    push8(posInf, 0x7Fu); push8(posInf, 0xF0u); push8(posInf, 0x00u); push8(posInf, 0x00u);
    push8(posInf, 0x00u); push8(posInf, 0x00u); push8(posInf, 0x00u); push8(posInf, 0x00u);
    CHECK(decodeThrows(posInf));

    std::vector<std::uint8_t> negNan;
    pushHeader(negNan);
    push8(negNan, 0x07u);
    pushU32(negNan, 8u);
    push8(negNan, 0xFFu); push8(negNan, 0xF8u); push8(negNan, 0x00u); push8(negNan, 0x00u);
    push8(negNan, 0x00u); push8(negNan, 0x00u); push8(negNan, 0x00u); push8(negNan, 0x01u);
    CHECK(decodeThrows(negNan));
}

static void test_finite_float_roundtrip() {
    TEST("finite float roundtrips through encode/decode");
    Canon::Record r = Canon::rec();
    Canon::put_float(r, 1, 3.25);
    Canon c = Canon::mk_record(std::move(r));
    std::vector<std::uint8_t> enc = canonical_encode(c);
    Canon d = canonical_decode(enc);
    CHECK(d.as_record()[0].second.kind() == CanonKind::Float);
    CHECK(d.as_record()[0].second.as_float() == 3.25);
}

int main() {
    std::printf("start canonical\n"); std::fflush(stdout);
    test_identical_fingerprint();
    test_field_order_independence();
    test_semantic_difference();
    test_identity_exact();
    test_malformed_rejected();
    test_nan_inf_rejected();
    test_finite_float_roundtrip();
    std::printf("canonical done\n"); std::fflush(stdout);
    RUN_TESTS();
}
