
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "compat/uuid.hpp"
#include "compat/sha256.hpp"

namespace compat {

class CanonError : public std::runtime_error { public: using std::runtime_error::runtime_error; };

enum class CanonKind : std::uint8_t {
    Null, Uint, Int, Bool, Str, Bytes, Uuid, Float, Record, Sequence
};

// A deterministic, strict, binary, hashable canonical value tree.
//
//   Record  : ordered map of uint32 tag -> value, encoded sorted by tag (order-stable).
//   Sequence: ordered list of values (insertion order is semantically meaningful).
//
// Fingerprinting uses SHA-256 over the canonical encoding. Strict decode rejects
// malformed lengths, duplicate tags, invalid enum values, unsupported versions,
// and trailing garbage.
class Canon {
public:
    using Record = std::vector<std::pair<std::uint32_t, Canon>>;
    using Seq = std::vector<Canon>;

    Canon() noexcept = default;

    CanonKind kind() const noexcept { return kind_; }
    bool is_null() const noexcept { return kind_ == CanonKind::Null; }
    std::uint64_t as_uint() const;
    std::int64_t as_int() const;
    bool as_bool() const;
    const std::string& as_string() const;   // Str or Bytes
    const Uuid& as_uuid() const;
    double as_float() const;
    const Record& as_record() const { return r_; }
    const Seq& as_seq() const { return q_; }

    static Canon mk_null();
    static Canon mk_uint(std::uint64_t v);
    static Canon mk_int(std::int64_t v);
    static Canon mk_bool(bool v);
    static Canon mk_str(std::string v);
    static Canon mk_bytes(std::string v);
    static Canon mk_uuid(const Uuid& v);
    static Canon mk_float(double v);          // rejects NaN / inf
    static Canon mk_record(Record r);          // sorted by tag, rejects duplicates
    static Canon mk_seq(Seq s);

    const Canon* field(std::uint32_t tag) const;

    static Record rec();
    static void put(Record& r, std::uint32_t tag, Canon v);
    static void put_uuid(Record& r, std::uint32_t tag, const Uuid& v);
    static void put_uint(Record& r, std::uint32_t tag, std::uint64_t v);
    static void put_int(Record& r, std::uint32_t tag, std::int64_t v);
    static void put_bool(Record& r, std::uint32_t tag, bool v);
    static void put_str(Record& r, std::uint32_t tag, std::string v);
    static void put_bytes(Record& r, std::uint32_t tag, std::string v);
    static void put_float(Record& r, std::uint32_t tag, double v);
    static void put_seq(Record& r, std::uint32_t tag, Seq s);
    static void put_record(Record& r, std::uint32_t tag, Record s);

private:
    CanonKind kind_ = CanonKind::Null;
    std::uint64_t u_ = 0;
    std::int64_t i_ = 0;
    bool b_ = false;
    std::string s_;
    Uuid id_;
    double f_ = 0.0;
    Record r_;
    Seq q_;
    void set(CanonKind k) { kind_ = k; }
};

// ---------------------------------------------------------------------------
// Canonical binary encoding / decoding
// ---------------------------------------------------------------------------
std::vector<std::uint8_t> canonical_encode(const Canon& c);

// Throws CanonError on malformed input. Returns the decoded Canon.
Canon canonical_decode(std::span<const std::uint8_t> bytes);

// SHA-256 fingerprint helpers.
Sha256::Digest canonical_fingerprint(const Canon& c);
std::string canonical_fingerprint_hex(const Canon& c);

// Human-readable, deterministic JSON-ish rendering (for explanations / debugging).
std::string canonical_to_json(const Canon& c);

} // namespace compat
