#pragma once
#include "compat/canonical.hpp"
#include "compat/id.hpp"
#include "compat/registry.hpp"
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace compat {

// Framed TCP protocol for the distributed Compatibility Registry.
// Strict framing: [magic 0x43524E57][version u8][type u8][len u32 BE][payload][crc32 u32 BE].
enum class MsgType : std::uint8_t {
    RegisterSource = 1,
    RegisterProfile = 2,
    RegisterRule = 3,
    RegisterEvidence = 4,
    QueryPair = 5,
    QueryRequirement = 6,
    Decision = 7,
    Invalidate = 8,
    Snapshot = 9,
    Ack = 10,
    Error = 11,
    Shutdown = 12
};

inline const char* msg_type_name(MsgType t) noexcept {
    switch (t) {
        case MsgType::RegisterSource: return "REGISTER_SOURCE";
        case MsgType::RegisterProfile: return "REGISTER_PROFILE";
        case MsgType::RegisterRule: return "REGISTER_RULE";
        case MsgType::RegisterEvidence: return "REGISTER_EVIDENCE";
        case MsgType::QueryPair: return "QUERY_PAIR";
        case MsgType::QueryRequirement: return "QUERY_REQUIREMENT";
        case MsgType::Decision: return "DECISION";
        case MsgType::Invalidate: return "INVALIDATE";
        case MsgType::Snapshot: return "SNAPSHOT";
        case MsgType::Ack: return "ACK";
        case MsgType::Error: return "ERROR";
        case MsgType::Shutdown: return "SHUTDOWN";
    }
    return "?";
}

struct NetMessage {
    MsgType type = MsgType::Ack;
    Canon payload;     // request/response fields
    // typed payload accessors
    std::size_t field_uuid(std::uint32_t tag, Uuid& out) const;
    std::string field_str(std::uint32_t tag) const;
};

std::vector<std::uint8_t> frame_message(MsgType type, const Canon& payload);
// Returns the decoded message and consumes frame bytes. Throws on framing error.
NetMessage unframe_message(std::span<const std::uint8_t> bytes, std::size_t& consumed);

// --- Coordinator server (runs in its own OS process) ---
// Serves framed requests on the given port. Each connection is handled
// serially; registry state is protected by its own mutex (no network I/O under it).
class CoordinatorServer {
public:
    explicit CoordinatorServer(std::uint16_t port);
    CoordinatorServer(std::uint16_t port, std::unique_ptr<CompatibilityRegistry> seed);
    ~CoordinatorServer();
    void run();            // accepts until shutdown
    void stop();
    std::uint16_t port() const { return port_; }
private:
    struct SourceInfo { Uuid boot_id; std::uint64_t generation = 1; std::string scope; };
    bool handle_request(const NetMessage& msg, NetMessage& response);
    std::uint16_t port_;
    std::uint64_t socket_ = 0;   // raw socket
    bool running_ = false;
    std::unique_ptr<CompatibilityRegistry> registry_ = std::make_unique<CompatibilityRegistry>();
    std::mutex mtx_;
    std::uint64_t epoch_ = 1;
    std::map<std::string, SourceInfo> sources_;
};

// --- Source / consumer client ---
class RegistryClient {
public:
    explicit RegistryClient(std::uint16_t port);
    ~RegistryClient();
    bool connect(std::string& err);
    void close();
    bool send(MsgType type, const Canon& payload, NetMessage& response, std::string& err);

    // Handy typed ops (all report coordinator epoch / source / generation in the response).
    bool register_source(const std::string& source, const std::string& scope, Uuid& boot_id, std::string& err);
    bool register_profile(const std::string& source, const Uuid& boot_id, std::uint64_t source_gen,
                          const Canon& profile, ProfileKind kind, const std::string& domain,
                          Uuid& profile_id, std::string& err);
    bool register_rule(const std::string& source, const Uuid& boot_id, std::uint64_t source_gen,
                       const CompatibilityRule& rule, std::string& err);
    bool register_evidence(const std::string& source, const Uuid& boot_id, std::uint64_t source_gen,
                           const EvidenceRecord& ev, std::string& err);
    bool query_pair(const Uuid& left, const Uuid& right, CompatibilityDecision& dec, std::string& err);
    bool query_requirement(const Uuid& candidate, const Uuid& requirement, CompatibilityDecision& dec, std::string& err);
    bool invalidate(const std::string& source, const Uuid& boot_id, std::uint64_t source_gen,
                    const Uuid& target, const std::string& reason, std::string& err);
    bool snapshot(std::string& err);
    bool shutdown(std::string& err);
    void set_epoch(std::uint64_t e) { epoch_ = e; }
    std::uint64_t epoch() const { return epoch_; }
private:
    std::uint16_t port_;
    std::uint64_t socket_ = 0;
    bool connected_ = false;
    std::uint64_t epoch_ = 0;   // coordinator epoch learned at RegisterSource
};

} // namespace compat
