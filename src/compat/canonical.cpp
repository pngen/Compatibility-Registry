#include "compat/canonical.hpp"
#include <bit>
#include <iomanip>
#include <sstream>

namespace compat {

static constexpr unsigned char kMagic[4] = { 0x43, 0x52, 0x43, 0x46 };  // 'C','R','C','F'
static constexpr unsigned char kVersion = 1;

std::uint64_t Canon::as_uint() const { if (kind_ != CanonKind::Uint) throw CanonError("Canon::as_uint: not Uint"); return u_; }
std::int64_t Canon::as_int() const { if (kind_ != CanonKind::Int) throw CanonError("Canon::as_int: not Int"); return i_; }
bool Canon::as_bool() const { if (kind_ != CanonKind::Bool) throw CanonError("Canon::as_bool: not Bool"); return b_; }
const std::string& Canon::as_string() const {
    if (kind_ != CanonKind::Str && kind_ != CanonKind::Bytes) throw CanonError("Canon::as_string: not String/Bytes");
    return s_;
}
const Uuid& Canon::as_uuid() const { if (kind_ != CanonKind::Uuid) throw CanonError("Canon::as_uuid: not Uuid"); return id_; }
double Canon::as_float() const { if (kind_ != CanonKind::Float) throw CanonError("Canon::as_float: not Float"); return f_; }

Canon Canon::mk_null() { Canon c; c.set(CanonKind::Null); return c; }
Canon Canon::mk_uint(std::uint64_t v) { Canon c; c.set(CanonKind::Uint); c.u_ = v; return c; }
Canon Canon::mk_int(std::int64_t v) { Canon c; c.set(CanonKind::Int); c.i_ = v; return c; }
Canon Canon::mk_bool(bool v) { Canon c; c.set(CanonKind::Bool); c.b_ = v; return c; }
Canon Canon::mk_str(std::string v) { Canon c; c.set(CanonKind::Str); c.s_ = std::move(v); return c; }
Canon Canon::mk_bytes(std::string v) { Canon c; c.set(CanonKind::Bytes); c.s_ = std::move(v); return c; }
Canon Canon::mk_uuid(const Uuid& v) { Canon c; c.set(CanonKind::Uuid); c.id_ = v; return c; }
Canon Canon::mk_float(double v) {
    if (!std::isfinite(v)) throw CanonError("Canon::mk_float: NaN/Inf not allowed in canonical encoding");
    Canon c; c.set(CanonKind::Float); c.f_ = v; return c;
}
Canon Canon::mk_record(Record r) {
    std::sort(r.begin(), r.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (std::size_t i = 1; i < r.size(); ++i) {
        if (r[i].first == r[i - 1].first) throw CanonError("Canon::mk_record: duplicate tag " + std::to_string(r[i].first));
    }
    Canon c; c.set(CanonKind::Record); c.r_ = std::move(r); return c;
}
Canon Canon::mk_seq(Seq s) { Canon c; c.set(CanonKind::Sequence); c.q_ = std::move(s); return c; }

void Canon::put(Record& r, std::uint32_t tag, Canon v) { r.emplace_back(tag, std::move(v)); }
void Canon::put_uuid(Record& r, std::uint32_t tag, const Uuid& v) { r.emplace_back(tag, mk_uuid(v)); }
void Canon::put_uint(Record& r, std::uint32_t tag, std::uint64_t v) { r.emplace_back(tag, mk_uint(v)); }
void Canon::put_int(Record& r, std::uint32_t tag, std::int64_t v) { r.emplace_back(tag, mk_int(v)); }
void Canon::put_bool(Record& r, std::uint32_t tag, bool v) { r.emplace_back(tag, mk_bool(v)); }
void Canon::put_str(Record& r, std::uint32_t tag, std::string v) { r.emplace_back(tag, mk_str(std::move(v))); }
void Canon::put_bytes(Record& r, std::uint32_t tag, std::string v) { r.emplace_back(tag, mk_bytes(std::move(v))); }
void Canon::put_float(Record& r, std::uint32_t tag, double v) { r.emplace_back(tag, mk_float(v)); }
void Canon::put_seq(Record& r, std::uint32_t tag, Seq s) { r.emplace_back(tag, mk_seq(std::move(s))); }
void Canon::put_record(Record& r, std::uint32_t tag, Record s) { r.emplace_back(tag, mk_record(std::move(s))); }

const Canon* Canon::field(std::uint32_t tag) const {
    if (kind_ != CanonKind::Record) return nullptr;
    auto it = std::lower_bound(r_.begin(), r_.end(), tag, [](const auto& e, std::uint32_t t) { return e.first < t; });
    if (it != r_.end() && it->first == tag) return &it->second;
    return nullptr;
}
Canon::Record Canon::rec() { return {}; }

// ---------------------------------------------------------------------------
// Encoders
// ---------------------------------------------------------------------------
static void w_u8(std::vector<std::uint8_t>& out, std::uint8_t v) { out.push_back(v); }

static void w_u32be(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}
static void w_u64be(std::vector<std::uint8_t>& out, std::uint64_t v) {
    for (int i = 7; i >= 0; --i) out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu));
}

static void append_element(const Canon& c, std::vector<std::uint8_t>& out) {
    switch (c.kind()) {
        case CanonKind::Null: w_u8(out, 0x00u); w_u32be(out, 0u); return;
        case CanonKind::Uint: {
            w_u8(out, 0x01u); w_u32be(out, 8u); w_u64be(out, c.as_uint()); return;
        }
        case CanonKind::Int: {
            w_u8(out, 0x02u); w_u32be(out, 8u); w_u64be(out, std::bit_cast<std::uint64_t>(c.as_int())); return;
        }
        case CanonKind::Bool: {
            w_u8(out, 0x03u); w_u32be(out, 1u); out.push_back(c.as_bool() ? 0x01u : 0x00u); return;
        }
        case CanonKind::Str:
        case CanonKind::Bytes: {
            const std::string& s = c.as_string();
            w_u8(out, c.kind() == CanonKind::Str ? 0x04u : 0x05u);
            w_u32be(out, static_cast<std::uint32_t>(s.size()));
            out.insert(out.end(), s.begin(), s.end());
            return;
        }
        case CanonKind::Uuid: {
            const auto& b = c.as_uuid().bytes();
            w_u8(out, 0x06u); w_u32be(out, 16u);
            out.insert(out.end(), b.begin(), b.end());
            return;
        }
        case CanonKind::Float: {
            w_u8(out, static_cast<std::uint8_t>(CanonKind::Float)); w_u32be(out, 8u);
            w_u64be(out, std::bit_cast<std::uint64_t>(c.as_float()));
            return;
        }
        case CanonKind::Record: {
            Canon::Record r = c.as_record();
            std::sort(r.begin(), r.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            for (std::size_t i = 1; i < r.size(); ++i) {
                if (r[i].first == r[i - 1].first) throw CanonError("canonical_encode: duplicate record tag");
            }
            std::vector<std::uint8_t> body;
            for (const auto& ent : r) {
                w_u32be(body, ent.first);
                append_element(ent.second, body);
            }
            w_u8(out, static_cast<std::uint8_t>(CanonKind::Record));
            w_u32be(out, static_cast<std::uint32_t>(body.size()));
            out.insert(out.end(), body.begin(), body.end());
            return;
        }
        case CanonKind::Sequence: {
            std::vector<std::uint8_t> body;
            for (const Canon& e : c.as_seq()) append_element(e, body);
            w_u8(out, static_cast<std::uint8_t>(CanonKind::Sequence));
            w_u32be(out, static_cast<std::uint32_t>(body.size()));
            out.insert(out.end(), body.begin(), body.end());
            return;
        }
    }
    throw CanonError("canonical_encode: unknown kind");
}

std::vector<std::uint8_t> canonical_encode(const Canon& c) {
    std::vector<std::uint8_t> out;
    out.reserve(64);
    out.insert(out.end(), kMagic, kMagic + 4);
    out.push_back(kVersion);
    append_element(c, out);
    return out;
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------
struct Reader {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t pos = 0;
    std::uint8_t u8() {
        if (pos >= size) throw CanonError("decode: unexpected end (u8)");
        return data[pos++];
    }
    std::uint32_t u32be() {
        if (size - pos < 4) throw CanonError("decode: unexpected end (u32)");
        std::uint32_t v = (static_cast<std::uint32_t>(data[pos]) << 24) |
                          (static_cast<std::uint32_t>(data[pos + 1]) << 16) |
                          (static_cast<std::uint32_t>(data[pos + 2]) << 8) |
                          static_cast<std::uint32_t>(data[pos + 3]);
        pos += 4;
        return v;
    }
    std::uint64_t u64be() {
        if (size - pos < 8) throw CanonError("decode: unexpected end (u64)");
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<std::uint64_t>(data[pos + i]);
        pos += 8;
        return v;
    }
    void skip(std::size_t n) {
        if (n > size - pos) throw CanonError("decode: skip exceeds buffer");
        pos += n;
    }
};

static Canon parse_element(Reader& rd, std::size_t end) {
    std::uint8_t kk = rd.u8();
    std::uint32_t len = rd.u32be();
    if (end - rd.pos < len) throw CanonError("decode: child element length exceeds segment");
    std::size_t payloadEnd = rd.pos + len;
    switch (static_cast<CanonKind>(kk)) {
        case CanonKind::Null: { if (len != 0) throw CanonError("decode: Null length != 0"); rd.pos = payloadEnd; return Canon::mk_null(); }
        case CanonKind::Uint: { if (len != 8) throw CanonError("decode: Uint length != 8"); std::uint64_t v = rd.u64be(); if (rd.pos != payloadEnd) throw CanonError("decode: Uint trailing bytes"); return Canon::mk_uint(v); }
        case CanonKind::Int: { if (len != 8) throw CanonError("decode: Int length != 8"); std::uint64_t bits = rd.u64be(); if (rd.pos != payloadEnd) throw CanonError("decode: Int trailing bytes"); return Canon::mk_int(std::bit_cast<std::int64_t>(bits)); }
        case CanonKind::Bool: { if (len != 1) throw CanonError("decode: Bool length != 1"); std::uint8_t v = rd.u8(); if (v != 0 && v != 1) throw CanonError("decode: invalid bool value"); if (rd.pos != payloadEnd) throw CanonError("decode: Bool trailing bytes"); return Canon::mk_bool(v == 1); }
        case CanonKind::Str:
        case CanonKind::Bytes: {
            std::string s;
            s.resize(len);
            if (len) { const std::uint8_t* p = rd.data + rd.pos; std::memcpy(s.data(), p, len); rd.pos = payloadEnd; }
            if (kk == static_cast<std::uint8_t>(CanonKind::Str)) return Canon::mk_str(std::move(s));
            return Canon::mk_bytes(std::move(s));
        }
        case CanonKind::Uuid: { if (len != 16) throw CanonError("decode: Uuid length != 16"); Uuid::Bytes by; for (std::size_t i = 0; i < 16; ++i) by[i] = rd.u8(); if (rd.pos != payloadEnd) throw CanonError("decode: Uuid trailing bytes"); return Canon::mk_uuid(Uuid(by)); }
        case CanonKind::Float: { if (len != 8) throw CanonError("decode: Float length != 8"); std::uint64_t bits = rd.u64be(); if (rd.pos != payloadEnd) throw CanonError("decode: Float trailing bytes"); double d = std::bit_cast<double>(bits); if (!std::isfinite(d)) throw CanonError("decode: Float NaN/Inf not allowed"); return Canon::mk_float(d); }
        case CanonKind::Record: {
            Canon::Record r;
            std::uint64_t lastTag = 0;
            bool first = true;
            while (rd.pos < payloadEnd) {
                std::uint32_t tag = rd.u32be();
                if (!first && tag <= lastTag) throw CanonError("decode: record tags not strictly increasing (duplicate/out-of-order)");
                lastTag = tag;
                first = false;
                r.emplace_back(tag, parse_element(rd, payloadEnd));
            }
            if (rd.pos != payloadEnd) throw CanonError("decode: record segment overrun");
            return Canon::mk_record(std::move(r));
        }
        case CanonKind::Sequence: {
            Canon::Seq q;
            while (rd.pos < payloadEnd) q.push_back(parse_element(rd, payloadEnd));
            if (rd.pos != payloadEnd) throw CanonError("decode: sequence segment overrun");
            return Canon::mk_seq(std::move(q));
        }
    }
    throw CanonError("decode: unknown kind " + std::to_string(kk));
}

Canon canonical_decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 6) throw CanonError("decode: buffer too small");
    if (std::memcmp(bytes.data(), kMagic, 4) != 0) throw CanonError("decode: bad magic");
    if (bytes[4] != kVersion) throw CanonError("decode: unsupported version " + std::to_string(bytes[4]));
    Reader rd{ bytes.data(), bytes.size(), 5 };
    Canon root = parse_element(rd, bytes.size());
    if (rd.pos != bytes.size()) throw CanonError("decode: trailing garbage");
    return root;
}

Sha256::Digest canonical_fingerprint(const Canon& c) {
    std::vector<std::uint8_t> enc = canonical_encode(c);
    return Sha256::compute(enc.data(), enc.size());
}
std::string canonical_fingerprint_hex(const Canon& c) {
    return Sha256::hex(canonical_fingerprint(c));
}

// ---------------------------------------------------------------------------
// JSON rendering (backslash-free source; prints standard JSON escapes at runtime)
// ---------------------------------------------------------------------------
static std::string json_escape(const std::string& s) {
    const unsigned char kBS = 0x5C;
    const unsigned char kQUOTE = 0x22;
    std::ostringstream ss;
    for (char ch : s) {
        unsigned char uc = static_cast<unsigned char>(ch);
        if (uc == kQUOTE) { ss << static_cast<char>(kBS) << static_cast<char>(kQUOTE); }
        else if (uc == kBS) { ss << static_cast<char>(kBS) << static_cast<char>(kBS); }
        else if (uc == 0x0A) { ss << static_cast<char>(kBS) << 'n'; }
        else if (uc == 0x0D) { ss << static_cast<char>(kBS) << 'r'; }
        else if (uc == 0x09) { ss << static_cast<char>(kBS) << 't'; }
        else if (uc < 0x20) { ss << static_cast<char>(kBS) << 'u' << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(uc) << std::dec; }
        else { ss << ch; }
    }
    return ss.str();
}

static std::string quote(const std::string& s) {
    return std::string(1, static_cast<char>(0x22)) + json_escape(s) + std::string(1, static_cast<char>(0x22));
}

std::string canonical_to_json(const Canon& c) {
    switch (c.kind()) {
        case CanonKind::Null: return "null";
        case CanonKind::Uint: return std::to_string(c.as_uint());
        case CanonKind::Int: return std::to_string(c.as_int());
        case CanonKind::Bool: return c.as_bool() ? "true" : "false";
        case CanonKind::Str: return quote(c.as_string());
        case CanonKind::Bytes: {
            std::ostringstream ss; ss << 'h' << 'e' << 'x' << ':';
            const std::string& s = c.as_string();
            static const char* h = "0123456789abcdef";
            for (unsigned char ch : s) { ss << h[ch >> 4] << h[ch & 0x0Fu]; }
            return quote(ss.str());
        }
        case CanonKind::Uuid: return quote(c.as_uuid().to_string());
        case CanonKind::Float: { std::ostringstream ss; ss << std::setprecision(17) << c.as_float(); return ss.str(); }
        case CanonKind::Record: {
            std::ostringstream ss; ss << '{';
            const auto& r = c.as_record();
            for (std::size_t i = 0; i < r.size(); ++i) {
                if (i) ss << ',';
                ss << static_cast<char>(0x22) << r[i].first << static_cast<char>(0x22) << ':' << canonical_to_json(r[i].second);
            }
            ss << '}'; return ss.str();
        }
        case CanonKind::Sequence: {
            std::ostringstream ss; ss << '[';
            const auto& q = c.as_seq();
            for (std::size_t i = 0; i < q.size(); ++i) {
                if (i) ss << ',';
                ss << canonical_to_json(q[i]);
            }
            ss << ']'; return ss.str();
        }
    }
    throw CanonError("canonical_to_json: unknown kind");
}

} // namespace compat
