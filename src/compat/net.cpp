#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "compat/net.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace compat {

static std::once_flag g_wsa;
static void init_wsa() {
    std::call_once(g_wsa, []() {
        WSADATA d; WSAStartup(MAKEWORD(2, 2), &d);
    });
}

static std::uint32_t crc32_js(const std::uint8_t* data, std::size_t n) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        c ^= static_cast<std::uint32_t>(data[i]);
        for (int b = 0; b < 8; ++b) {
            if (c & 1u) c = (c >> 1) ^ 0xEDB88320u;
            else c >>= 1;
        }
    }
    return c ^ 0xFFFFFFFFu;
}

static void w_u8(std::vector<std::uint8_t>& o, std::uint8_t v) { o.push_back(v); }
static void w_u32be(std::vector<std::uint8_t>& o, std::uint32_t v) {
    o.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    o.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    o.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    o.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

std::vector<std::uint8_t> frame_message(MsgType type, const Canon& payload) {
    std::vector<std::uint8_t> pl = canonical_encode(payload);
    std::vector<std::uint8_t> out;
    out.reserve(10 + pl.size() + 4);
    w_u32be(out, 0x43524E57u);           // "CRNW"
    w_u8(out, 1);                        // protocol version
    w_u8(out, static_cast<std::uint8_t>(type));
    w_u32be(out, static_cast<std::uint32_t>(pl.size()));
    out.insert(out.end(), pl.begin(), pl.end());
    std::uint32_t crc = crc32_js(pl.data(), pl.size());
    w_u32be(out, crc);
    return out;
}

struct BufReader {
    const std::uint8_t* d; std::size_t n; std::size_t p = 0;
    std::uint8_t u8() { if (p >= n) throw std::runtime_error("unframe: short (u8)"); return d[p++]; }
    std::uint32_t u32be() {
        if (n - p < 4) throw std::runtime_error("unframe: short (u32)");
        std::uint32_t v = (static_cast<std::uint32_t>(d[p]) << 24) | (static_cast<std::uint32_t>(d[p+1]) << 16) |
                          (static_cast<std::uint32_t>(d[p+2]) << 8) | static_cast<std::uint32_t>(d[p+3]);
        p += 4; return v;
    }
};

NetMessage unframe_message(std::span<const std::uint8_t> bytes, std::size_t& consumed) {
    if (bytes.size() < 10) throw std::runtime_error("unframe: header too short");
    BufReader r{ bytes.data(), bytes.size(), 0 };
    std::uint32_t magic = r.u32be();
    if (magic != 0x43524E57u) throw std::runtime_error("unframe: bad magic");
    std::uint8_t ver = r.u8();
    if (ver != 1) throw std::runtime_error("unframe: bad version");
    std::uint8_t type = r.u8();
    std::uint32_t len = r.u32be();
    if (len > bytes.size() - 10) throw std::runtime_error("unframe: length exceeds buffer");
    if (bytes.size() - 10 - len < 4) throw std::runtime_error("unframe: missing checksum");
    const std::uint8_t* pl = bytes.data() + 10;
    std::uint32_t stored = (static_cast<std::uint32_t>(bytes[10 + len]) << 24) |
                           (static_cast<std::uint32_t>(bytes[10 + len + 1]) << 16) |
                           (static_cast<std::uint32_t>(bytes[10 + len + 2]) << 8) |
                           static_cast<std::uint32_t>(bytes[10 + len + 3]);
    std::uint32_t calc = crc32_js(pl, len);
    if (calc != stored) throw std::runtime_error("unframe: checksum mismatch");
    NetMessage m;
    m.type = static_cast<MsgType>(type);
    m.payload = canonical_decode(std::span<const std::uint8_t>(pl, len));
    consumed = 10 + len + 4;
    return m;
}

std::size_t NetMessage::field_uuid(std::uint32_t tag, Uuid& out) const {
    const Canon* v = payload.field(tag);
    if (!v || v->kind() != CanonKind::Uuid) return 0;
    out = v->as_uuid(); return 1;
}
std::string NetMessage::field_str(std::uint32_t tag) const {
    const Canon* v = payload.field(tag);
    if (!v || v->kind() != CanonKind::Str) return {};
    return v->as_string();
}

// ---------------------------------------------------------------------------
// Socket helpers
// ---------------------------------------------------------------------------
static bool sock_send_all(std::uint64_t s, const std::uint8_t* d, std::size_t n) {
    std::size_t off = 0;
    while (off < n) {
        int sent = ::send(static_cast<SOCKET>(s), reinterpret_cast<const char*>(d + off), static_cast<int>(n - off), 0);
        if (sent <= 0) return false;
        off += static_cast<std::size_t>(sent);
    }
    return true;
}
static bool sock_recv_exact(std::uint64_t s, std::uint8_t* d, std::size_t n) {
    std::size_t off = 0;
    while (off < n) {
        int got = ::recv(static_cast<SOCKET>(s), reinterpret_cast<char*>(d + off), static_cast<int>(n - off), 0);
        if (got <= 0) return false;
        off += static_cast<std::size_t>(got);
    }
    return true;
}
static bool recv_frame(std::uint64_t s, NetMessage& out, std::string& err) {
    std::array<std::uint8_t, 10> hdr{};
    if (!sock_recv_exact(s, hdr.data(), 10)) { err = "recv header failed"; return false; }
    std::size_t len = (static_cast<std::size_t>(hdr[6]) << 24) | (static_cast<std::size_t>(hdr[7]) << 16) |
                      (static_cast<std::size_t>(hdr[8]) << 8) | static_cast<std::size_t>(hdr[9]);
    if (len > 64 * 1024 * 1024) { err = "frame too large"; return false; }
    std::vector<std::uint8_t> frame(10 + len + 4);
    std::memcpy(frame.data(), hdr.data(), 10);
    if (!sock_recv_exact(s, frame.data() + 10, len + 4)) { err = "recv body failed"; return false; }
    std::size_t consumed = 0;
    try { out = unframe_message(frame, consumed); } catch (const std::exception& e) { err = e.what(); return false; }
    return true;
}

static Canon enc_constraint_wire(const Constraint& c) {
    Canon::Record r = Canon::rec();
    Canon::put_uint(r, 1, static_cast<std::uint64_t>(c.op));
    Canon::put_str(r, 2, c.field);
    Canon::put_str(r, 3, c.right_field);
    Canon::put(r, 4, c.expected);
    return Canon::mk_record(std::move(r));
}
static Constraint dec_constraint_wire(const Canon& c) {
    Constraint x;
    const Canon* v = c.field(1); if (v && v->kind()==CanonKind::Uint) x.op = static_cast<PredOp>(v->as_uint());
    v = c.field(2); if (v && v->kind()==CanonKind::Str) x.field = v->as_string();
    v = c.field(3); if (v && v->kind()==CanonKind::Str) x.right_field = v->as_string();
    v = c.field(4); if (v) x.expected = *v;
    return x;
}

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------
RegistryClient::RegistryClient(std::uint16_t port) : port_(port) {}
RegistryClient::~RegistryClient() { close(); }

bool RegistryClient::connect(std::string& err) {
    init_wsa();
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { err = "socket failed"; return false; }
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        err = "connect failed"; ::closesocket(s); return false;
    }
    socket_ = static_cast<std::uint64_t>(s);
    connected_ = true;
    return true;
}
void RegistryClient::close() {
    if (connected_ && socket_) { ::closesocket(static_cast<SOCKET>(socket_)); }
    socket_ = 0; connected_ = false;
}

bool RegistryClient::send(MsgType type, const Canon& payload, NetMessage& response, std::string& err) {
    if (!connected_) { err = "not connected"; return false; }
    std::vector<std::uint8_t> frame = frame_message(type, payload);
    if (!sock_send_all(socket_, frame.data(), frame.size())) { err = "send failed"; return false; }
    NetMessage resp;
    if (!recv_frame(socket_, resp, err)) return false;
    if (resp.type == MsgType::Error) { err = resp.field_str(1); return false; }
    response = std::move(resp);
    return true;
}

bool RegistryClient::register_source(const std::string& source, const std::string& scope, Uuid& boot_id, std::string& err) {
    Canon::Record r = Canon::rec();
    Canon::put_str(r, 1, source);
    Canon::put_str(r, 2, scope);
    NetMessage resp;
    if (!send(MsgType::RegisterSource, Canon::mk_record(std::move(r)), resp, err)) return false;
    std::uint64_t epoch2 = 0;
    const Canon* ev = resp.payload.field(2);
    if (ev && ev->kind()==CanonKind::Uint) epoch2 = ev->as_uint();
    epoch_ = epoch2;
    resp.field_uuid(1, boot_id);
    return true;
}

bool RegistryClient::register_profile(const std::string& source, const Uuid& boot_id, std::uint64_t source_gen,
                                      const Canon& profile, ProfileKind kind, const std::string& domain,
                                      Uuid& profile_id, std::string& err) {
    Canon::Record r = Canon::rec();
    Canon::put_str(r, 1, source);
    Canon::put_uuid(r, 2, boot_id);
    Canon::put_uint(r, 3, source_gen);
    Canon::put_str(r, 4, domain);
    Canon::put_record(r, 5, profile.as_record());
    Canon::put_uint(r, 6, static_cast<std::uint64_t>(kind));
    Canon::put_uint(r, 100u, epoch_);
    NetMessage resp;
    if (!send(MsgType::RegisterProfile, Canon::mk_record(std::move(r)), resp, err)) return false;
    resp.field_uuid(1, profile_id);
    return true;
}

bool RegistryClient::register_rule(const std::string& source, const Uuid& boot_id, std::uint64_t source_gen,
                                   const CompatibilityRule& rule, std::string& err) {
    // Encode rule minimally: rule_id, generation, scope, domain, priority, outcome.
    Canon::Record r = Canon::rec();
    Canon::put_str(r, 1, source); Canon::put_uuid(r, 2, boot_id); Canon::put_uint(r, 3, source_gen);
    Canon::put_uuid(r, 4, rule.rule_id.value);
    Canon::put_uint(r, 5, rule.generation.value);
    Canon::put_str(r, 6, rule.scope);
    Canon::put_str(r, 7, rule.domain);
    Canon::put_int(r, 8, rule.priority);
    Canon::put_uint(r, 9, static_cast<std::uint64_t>(rule.outcome));
    { Canon::Seq s; for (auto& c : rule.required) s.push_back(enc_constraint_wire(c)); Canon::put_seq(r, 10, std::move(s)); }
    { Canon::Seq s; for (auto& c : rule.incompatible_with) s.push_back(enc_constraint_wire(c)); Canon::put_seq(r, 11, std::move(s)); }
    if (rule.left_kind) Canon::put_uint(r, 12, static_cast<std::uint64_t>(*rule.left_kind));
    if (rule.right_kind) Canon::put_uint(r, 13, static_cast<std::uint64_t>(*rule.right_kind));
    Canon::put_uint(r, 100u, epoch_);
    NetMessage resp;
    if (!send(MsgType::RegisterRule, Canon::mk_record(std::move(r)), resp, err)) return false;
    return true;
}

bool RegistryClient::register_evidence(const std::string& source, const Uuid& boot_id, std::uint64_t source_gen,
                                       const EvidenceRecord& ev, std::string& err) {
    Canon::Record r = Canon::rec();
    Canon::put_str(r, 1, source); Canon::put_uuid(r, 2, boot_id); Canon::put_uint(r, 3, source_gen);
    Canon::put_str(r, 4, ev.field);
    Canon::put_record(r, 5, ev.value.as_record());
    Canon::put_uint(r, 100u, epoch_);
    NetMessage resp;
    if (!send(MsgType::RegisterEvidence, Canon::mk_record(std::move(r)), resp, err)) return false;
    return true;
}

bool RegistryClient::query_pair(const Uuid& left, const Uuid& right, CompatibilityDecision& dec, std::string& err) {
    Canon::Record r = Canon::rec();
    Canon::put_uuid(r, 1, left); Canon::put_uuid(r, 2, right);
    Canon::put_uint(r, 100u, epoch_);
    NetMessage resp;
    if (!send(MsgType::QueryPair, Canon::mk_record(std::move(r)), resp, err)) return false;
    dec.left = left; dec.right = right;
    std::uint64_t oc = 0; const Canon* v = resp.payload.field(1);
    if (v && v->kind()==CanonKind::Uint) oc = v->as_uint();
    dec.outcome = static_cast<Outcome>(oc);
    resp.field_uuid(2, dec.decision_id);
    resp.field_uuid(3, dec.rule_id);
    v = resp.payload.field(4); if (v && v->kind()==CanonKind::Str) dec.explanation = v->as_string();
    return true;
}

bool RegistryClient::query_requirement(const Uuid& candidate, const Uuid& requirement, CompatibilityDecision& dec, std::string& err) {
    Canon::Record r = Canon::rec();
    Canon::put_uuid(r, 1, candidate); Canon::put_uuid(r, 2, requirement);
    Canon::put_uint(r, 100u, epoch_);
    NetMessage resp;
    if (!send(MsgType::QueryRequirement, Canon::mk_record(std::move(r)), resp, err)) return false;
    dec.left = candidate; dec.right = requirement;
    std::uint64_t oc = 0; const Canon* v = resp.payload.field(1);
    if (v && v->kind()==CanonKind::Uint) oc = v->as_uint();
    dec.outcome = static_cast<Outcome>(oc);
    resp.field_uuid(2, dec.decision_id);
    return true;
}

bool RegistryClient::invalidate(const std::string& source, const Uuid& boot_id, std::uint64_t source_gen,
                                const Uuid& target, const std::string& reason, std::string& err) {
    Canon::Record r = Canon::rec();
    Canon::put_str(r, 1, source); Canon::put_uuid(r, 2, boot_id); Canon::put_uint(r, 3, source_gen);
    Canon::put_uuid(r, 4, target); Canon::put_str(r, 5, reason);
    Canon::put_uint(r, 100u, epoch_);
    NetMessage resp;
    if (!send(MsgType::Invalidate, Canon::mk_record(std::move(r)), resp, err)) return false;
    return true;
}

bool RegistryClient::snapshot(std::string& err) {
    Canon::Record r = Canon::rec();
    NetMessage resp;
    if (!send(MsgType::Snapshot, Canon::mk_record(std::move(r)), resp, err)) return false;
    return true;
}

bool RegistryClient::shutdown(std::string& err) {
    Canon::Record r = Canon::rec();
    NetMessage resp;
    return send(MsgType::Shutdown, Canon::mk_record(std::move(r)), resp, err);
}

namespace {
static Canon err_rec(const std::string& m) { Canon::Record r = Canon::rec(); Canon::put_str(r, 1, m); return Canon::mk_record(std::move(r)); }
static Canon ack_rec() { return Canon::mk_record(Canon::rec()); }
static Canon enc_decision_wire(const CompatibilityDecision& d) {
    Canon::Record r = Canon::rec();
    Canon::put_uint(r, 1, static_cast<std::uint64_t>(d.outcome));
    Canon::put_uuid(r, 2, d.decision_id);
    Canon::put_uuid(r, 3, d.rule_id);
    Canon::put_str(r, 4, d.explanation);
    return Canon::mk_record(std::move(r));
}
static constexpr std::uint32_t kEpochTag = 100;
} // namespace

CoordinatorServer::CoordinatorServer(std::uint16_t port) : port_(port) {}
CoordinatorServer::CoordinatorServer(std::uint16_t port, std::unique_ptr<CompatibilityRegistry> seed) : port_(port) {
    if (seed) registry_ = std::move(seed);
}
CoordinatorServer::~CoordinatorServer() { stop(); }

void CoordinatorServer::run() {
    init_wsa();
    SOCKET ls = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) return;
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port_); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(ls, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == SOCKET_ERROR) { ::closesocket(ls); return; }
    if (::listen(ls, SOMAXCONN) == SOCKET_ERROR) { ::closesocket(ls); return; }
    socket_ = static_cast<std::uint64_t>(ls);
    running_ = true;
    while (running_) {
        SOCKET cs = ::accept(ls, nullptr, nullptr);
        if (cs == INVALID_SOCKET) { if (!running_) break; else continue; }
        // Serve one connection at a time. A "source" is identified by name inside
        // each request, so a single connection may carry many logical sources.
        // Network I/O never happens under the registry lock.
        while (running_) {
            NetMessage msg; std::string err;
            if (!recv_frame(static_cast<std::uint64_t>(cs), msg, err)) break;
            NetMessage resp;
            bool ok = handle_request(msg, resp);
            auto frame = frame_message(ok ? resp.type : MsgType::Error, ok ? resp.payload : err_rec("request failed"));
            if (!sock_send_all(static_cast<std::uint64_t>(cs), frame.data(), frame.size())) break;
            if (msg.type == MsgType::Shutdown) { running_ = false; break; }
        }
        ::closesocket(cs);
    }
    ::closesocket(ls);
    socket_ = 0;
}

void CoordinatorServer::stop() { running_ = false; if (socket_) { ::closesocket(static_cast<SOCKET>(socket_)); socket_ = 0; } }

bool CoordinatorServer::handle_request(const NetMessage& msg, NetMessage& response) {
    response.type = MsgType::Ack;
    switch (msg.type) {
        case MsgType::RegisterSource: {
            std::string source = msg.field_str(1);
            std::string scope = msg.field_str(2);
            if (source.empty()) { response.payload = err_rec("missing source name"); return false; }
            Uuid boot = Uuid::genseed();
            { std::lock_guard<std::mutex> lk(mtx_); sources_[source] = SourceInfo{boot, 1, scope}; }
            Canon::Record r = Canon::rec();
            Canon::put_uuid(r, 1, boot);
            Canon::put_uint(r, 2, epoch_);
            Canon::put_uint(r, 3, 1);
            response.payload = Canon::mk_record(std::move(r));
            return true;
        }
        case MsgType::RegisterProfile: {
            std::string source = msg.field_str(1);
            Uuid boot; msg.field_uuid(2, boot);
            std::uint64_t sgen = 0; const Canon* v = msg.payload.field(3);
            if (v && v->kind()==CanonKind::Uint) sgen = v->as_uint();
            std::string domain = msg.field_str(4);
            const Canon* prof = msg.payload.field(5);
            const Canon* kindv = msg.payload.field(6);
            std::uint64_t epoch = 0; const Canon* ev = msg.payload.field(kEpochTag);
            if (ev && ev->kind()==CanonKind::Uint) epoch = ev->as_uint();
            if (!prof || prof->kind()!=CanonKind::Record || !kindv || kindv->kind()!=CanonKind::Uint) { response.payload = err_rec("bad profile"); return false; }
            ProfileKind kind = static_cast<ProfileKind>(kindv->as_uint());
            { std::lock_guard<std::mutex> lk(mtx_);
              if (epoch != epoch_) { response.payload = err_rec("stale coordinator epoch"); return false; }
              auto it = sources_.find(source);
              if (it == sources_.end()) { response.payload = err_rec("unknown source"); return false; }
              if (it->second.boot_id != boot) { response.payload = err_rec("stale SourceBootId"); return false; }
              if (sgen < it->second.generation) { response.payload = err_rec("stale source generation"); return false; }
              if (it->second.scope != "*" && it->second.scope != domain) { response.payload = err_rec("source not authorized for domain"); return false; }
            }
            ProfileId pid = registry_->register_profile(kind, *prof, {}, "");
            { std::lock_guard<std::mutex> lk(mtx_); auto it = sources_.find(source); if (it != sources_.end()) it->second.generation = sgen; }
            Canon::Record r = Canon::rec(); Canon::put_uuid(r, 1, pid.value); response.payload = Canon::mk_record(std::move(r));
            return true;
        }
        case MsgType::RegisterRule: {
            std::string source = msg.field_str(1);
            Uuid boot; msg.field_uuid(2, boot);
            Uuid rid; msg.field_uuid(4, rid);
            std::uint64_t epoch = 0; const Canon* ev = msg.payload.field(kEpochTag);
            if (ev && ev->kind()==CanonKind::Uint) epoch = ev->as_uint();
            { std::lock_guard<std::mutex> lk(mtx_);
              if (epoch != epoch_) { response.payload = err_rec("stale coordinator epoch"); return false; }
              auto it = sources_.find(source);
              if (it == sources_.end() || it->second.boot_id != boot) { response.payload = err_rec("stale SourceBootId"); return false; }
            }
            CompatibilityRule rule; rule.rule_id = CompatibilityRuleId(rid);
            rule.scope = msg.field_str(6); rule.domain = msg.field_str(7);
            const Canon* pv = msg.payload.field(8); if (pv && pv->kind()==CanonKind::Int) rule.priority = static_cast<int>(pv->as_int());
            const Canon* ov = msg.payload.field(9); if (ov && ov->kind()==CanonKind::Uint) rule.outcome = static_cast<Outcome>(ov->as_uint());
            const Canon* vq = msg.payload.field(10); if (vq && vq->kind()==CanonKind::Sequence) for (auto& e : vq->as_seq()) rule.required.push_back(dec_constraint_wire(e));
            const Canon* vi = msg.payload.field(11); if (vi && vi->kind()==CanonKind::Sequence) for (auto& e : vi->as_seq()) rule.incompatible_with.push_back(dec_constraint_wire(e));
            const Canon* lkv = msg.payload.field(12); if (lkv && lkv->kind()==CanonKind::Uint) rule.left_kind = static_cast<ProfileKind>(lkv->as_uint());
            const Canon* rkv = msg.payload.field(13); if (rkv && rkv->kind()==CanonKind::Uint) rule.right_kind = static_cast<ProfileKind>(rkv->as_uint());
            registry_->register_rule(rule);
            response.payload = ack_rec();
            return true;
        }
        case MsgType::RegisterEvidence: {
            std::string source = msg.field_str(1);
            Uuid boot; msg.field_uuid(2, boot);
            std::uint64_t epoch = 0; const Canon* ev = msg.payload.field(kEpochTag);
            if (ev && ev->kind()==CanonKind::Uint) epoch = ev->as_uint();
            { std::lock_guard<std::mutex> lk(mtx_);
              if (epoch != epoch_) { response.payload = err_rec("stale coordinator epoch"); return false; }
              auto it = sources_.find(source);
              if (it == sources_.end() || it->second.boot_id != boot) { response.payload = err_rec("stale SourceBootId"); return false; }
            }
            EvidenceRecord e; e.id = EvidenceId::genseed();
            e.field = msg.field_str(4);
            const Canon* v = msg.payload.field(5); if (v) e.value = *v;
            registry_->register_evidence(std::move(e));
            response.payload = ack_rec();
            return true;
        }
        case MsgType::QueryPair: {
            Uuid left, right; msg.field_uuid(1, left); msg.field_uuid(2, right);
            CompatibilityDecision d = registry_->evaluate_pair(left, right);
            response.type = MsgType::Decision;
            response.payload = enc_decision_wire(d);
            return true;
        }
        case MsgType::QueryRequirement: {
            Uuid cand, req; msg.field_uuid(1, cand); msg.field_uuid(2, req);
            CompatibilityDecision d = registry_->evaluate_requirement(cand, req);
            response.type = MsgType::Decision;
            response.payload = enc_decision_wire(d);
            return true;
        }
        case MsgType::Invalidate: {
            std::string source = msg.field_str(1);
            Uuid boot; msg.field_uuid(2, boot);
            Uuid target; msg.field_uuid(4, target);
            std::string reason = msg.field_str(5);
            std::uint64_t epoch = 0; const Canon* ev = msg.payload.field(kEpochTag);
            if (ev && ev->kind()==CanonKind::Uint) epoch = ev->as_uint();
            { std::lock_guard<std::mutex> lk(mtx_);
              if (epoch != epoch_) { response.payload = err_rec("stale coordinator epoch"); return false; }
              auto it = sources_.find(source);
              if (it == sources_.end() || it->second.boot_id != boot) { response.payload = err_rec("stale SourceBootId"); return false; }
            }
            registry_->invalidate_profile(target, reason);
            response.payload = ack_rec();
            return true;
        }
        case MsgType::Snapshot: {
            std::vector<std::uint8_t> snap = registry_->snapshot();
            Canon::Record r = Canon::rec();
            Canon::put_bytes(r, 1, std::string(reinterpret_cast<const char*>(snap.data()), snap.size()));
            response.payload = Canon::mk_record(std::move(r));
            return true;
        }
        case MsgType::Shutdown: {
            response.payload = ack_rec();
            return true;
        }
        default: { response.payload = err_rec("unknown message type"); return false; }
    }
}

} // namespace compat
