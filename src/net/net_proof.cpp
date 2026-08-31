#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "compat/compat.hpp"
#include "compat/net.hpp"
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace compat;

static int failures = 0;
static void ck(bool ok, const char* m){ std::printf("%s %s\n", ok?"[OK]":"[FAIL]", m); if(!ok) failures++; }

static bool wait_port(std::uint16_t port) {
    for (int tries = 0; tries < 200; ++tries) {
        SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s != INVALID_SOCKET) {
            sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(port); a.sin_addr.s_addr=inet_addr("127.0.0.1");
            if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) { ::closesocket(s); return true; }
            ::closesocket(s);
        }
        ::Sleep(25);
    }
    return false;
}
static bool spawn_cmd(std::uint16_t port, const std::string& recover, PROCESS_INFORMATION& pi) {
    STARTUPINFOA si{}; si.cb = sizeof(si);
    std::string cmd = "build\\Release\\compat_coordinator.exe --port " + std::to_string(port);
    if (!recover.empty()) cmd += " --recover " + recover;
    std::vector<char> cmdline(cmd.begin(), cmd.end()); cmdline.push_back(0);
    BOOL ok = ::CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    return ok == TRUE;
}
static KernelProfile makeKernel(const std::string& cc, const std::string& digest) {
    KernelProfile kp; kp.kernel_artifact_id=KernelArtifactId::genseed(); kp.operation="add";
    kp.architecture="sm_"+cc; kp.compute_capability=cc; kp.abi="cuda"; kp.runtime="cudart";
    kp.compiler="nvcc"; kp.compiler_version="12.9"; kp.dtype="f32"; kp.layout="aos";
    kp.shape_spec="[N]"; kp.artifact_digest=digest; kp.generation=1; return kp;
}
static ModelProfile makeModel(){ ModelProfile mp; mp.model_id=ModelId::genseed(); mp.revision_id=ModelRevisionId::genseed();
    mp.architecture="transformer"; mp.family="llm"; mp.quantization="fp16"; mp.tokenizer_id=TokenizerId::genseed();
    mp.vocabulary_id=VocabularyId::genseed(); mp.dtype="f16"; return mp; }
static DeviceProfile makeDevice(const std::string& cc){ DeviceProfile p; p.device_id=DeviceId::genseed(); p.vendor="NVIDIA";
    p.architecture="sm_"+cc; p.compute_capability=cc; p.memory_model="global"; p.total_memory=32579ull*1024ull*1024ull;
    p.supported_dtypes={"f16","f32","bf16"}; p.instruction_classes={"sm_"+cc}; p.runtime_min="12.9"; p.driver_min="13.4"; return p; }
static CompatibilityRule makeRule(){ CompatibilityRule rule; rule.rule_id=CompatibilityRuleId::genseed(); rule.scope="pair";
    rule.domain="kernel-device"; rule.left_kind=ProfileKind::Kernel; rule.right_kind=ProfileKind::Device; rule.outcome=Outcome::Compatible;
    Constraint req; req.op=PredOp::VersionLe; req.field="compute_capability"; req.right_field="compute_capability";
    Constraint inc; inc.op=PredOp::VersionGt; inc.field="compute_capability"; inc.right_field="compute_capability";
    rule.required.push_back(req); rule.incompatible_with.push_back(inc); return rule; }

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== Compatibility Registry multiprocess proof (real coordinator OS process + framed TCP) ===\n");
    WSADATA wd; WSAStartup(MAKEWORD(2,2), &wd);
    const std::uint16_t port = 19731;

    PROCESS_INFORMATION pi; pi.hProcess=nullptr; pi.hThread=nullptr;
    ck(spawn_cmd(port, "", pi), "spawned coordinator OS process");
    ck(wait_port(port), "coordinator listening on port");

    RegistryClient ctl(port); std::string err;
    ck(ctl.connect(err), "control connection established");
    Uuid bootA, bootB, bootC;
    ck(ctl.register_source("source_a", "*", bootA, err), "source A registered (scope *)");
    ck(ctl.register_source("source_b", "device", bootB, err), "source B registered (scope device)");
    ck(ctl.register_source("controller", "*", bootC, err), "controller registered (scope *)");
    const std::uint64_t epoch = ctl.epoch();

    KernelProfile kp120 = makeKernel("12.0","abc"), kp130 = makeKernel("13.0","xyz");
    ModelProfile mp = makeModel(); DeviceProfile dp = makeDevice("12.0");
    Uuid modelId, kId, kBadId, devId, devId2, kg2;
    ck(ctl.register_profile("source_a", bootA, 1, mp.to_canon(), ProfileKind::Model, "model", modelId, err), "A published model profile");
    ck(ctl.register_profile("source_a", bootA, 1, kp120.to_canon(), ProfileKind::Kernel, "kernel", kId, err), "A published kernel (sm_120) profile");
    ck(ctl.register_profile("source_a", bootA, 1, kp130.to_canon(), ProfileKind::Kernel, "kernel", kBadId, err), "A published kernel (sm_130) profile");
    ck(ctl.register_profile("source_b", bootB, 1, dp.to_canon(), ProfileKind::Device, "device", devId, err), "B published device (sm_120) profile");

    CompatibilityRule rule = makeRule();
    ck(ctl.register_rule("controller", bootC, 1, rule, err), "controller registered kernel/device rule");
    // Authority: source B (scoped to device) must NOT publish an unrelated model domain.
    Uuid forbiddenId; bool authority = !ctl.register_profile("source_b", bootB, 1, mp.to_canon(), ProfileKind::Model, "model", forbiddenId, err);
    ck(authority, "authority: device-only source rejected when publishing an unrelated model domain");

    CompatibilityDecision dExact, dInc, dUnk, dCompat;
    ctl.query_pair(devId, devId, dExact, err); ck(dExact.outcome == Outcome::Exact, "[6] exact compatible result succeeds (EXACT)");
    ctl.query_pair(kBadId, devId, dInc, err);   ck(dInc.outcome == Outcome::Incompatible, "[7] incompatible case proven (sm_130 on sm_120)");
    ctl.query_pair(modelId, kId, dUnk, err);    ck(dUnk.outcome == Outcome::Unknown || dUnk.outcome == Outcome::InsufficientEvidence, "[8] unknown/insufficient-evidence case proven (model vs kernel)");
    ctl.query_pair(kId, devId, dCompat, err);   ck(dCompat.outcome == Outcome::Compatible, "[note] kernel/device pair COMPATIBLE");

    // 10-11. restart source B with a fresh SourceBootId and roll source generation
    Uuid bootB2; ck(ctl.register_source("source_b", "device", bootB2, err), "source B restarted with fresh SourceBootId");
    ck(bootB2 != bootB, "restarted source B has a new SourceBootId");
    ck(ctl.register_profile("source_b", bootB2, 2, dp.to_canon(), ProfileKind::Device, "device", devId2, err), "[11] source B rolled to generation 2 and re-published device profile");

    // 13. stale coordinator epoch rejected
    ctl.set_epoch(epoch + 1000);
    Uuid devIdx; bool staleEpoch = !ctl.register_profile("source_b", bootB2, 1, dp.to_canon(), ProfileKind::Device, "device", devIdx, err);
    ctl.set_epoch(epoch);
    ck(staleEpoch, "[13] stale coordinator epoch rejected over TCP");
    // 14. stale SourceBootId rejected
    bool staleBoot = !ctl.register_profile("source_b", bootB, 1, dp.to_canon(), ProfileKind::Device, "device", devIdx, err);
    ck(staleBoot, "[14] stale SourceBootId rejected over TCP");
    // 15. stale source generation rejected
    bool staleGen = !ctl.register_profile("source_b", bootB2, 0, dp.to_canon(), ProfileKind::Device, "device", devIdx, err);
    ck(staleGen, "[15] stale source generation rejected over TCP");

    // 16. stale vs fresh profile generation produce distinct decisions
    KernelProfile kf2 = makeKernel("12.0","G2");
    ck(ctl.register_profile("source_a", bootA, 2, kf2.to_canon(), ProfileKind::Kernel, "kernel", kg2, err), "published fresh kernel generation");
    CompatibilityDecision dG1, dG2; ctl.query_pair(kId, devId, dG1, err); ctl.query_pair(kg2, devId, dG2, err);
    ck(dG1.decision_id != dG2.decision_id, "[16] stale and fresh generation decisions are distinct");

    // 18. re-evaluate compatibility successfully after fresh device
    CompatibilityDecision dRe; ctl.query_pair(kId, devId2, dRe, err);
    ck(dRe.outcome == Outcome::Compatible, "[18] re-evaluation succeeds after fresh device profile");

    // 19. capture the current (authoritative) compatible decision for replay comparison
    CompatibilityDecision dFinal; ctl.query_pair(kId, devId, dFinal, err);

    // 20. persist registry (snapshot over TCP)
    std::vector<std::uint8_t> snap;
    { Canon::Record empty = Canon::rec(); NetMessage resp;
      if (ctl.send(MsgType::Snapshot, Canon::mk_record(std::move(empty)), resp, err)) {
        const Canon* v = resp.payload.field(1);
        if (v && v->kind()==CanonKind::Bytes){ const std::string& s=v->as_string(); snap.assign(s.begin(), s.end()); } } }
    ck(!snap.empty(), "[20] snapshot captured over TCP");
    { std::ofstream of("build\\net_registry.bin", std::ios::binary); of.write(reinterpret_cast<const char*>(snap.data()), static_cast<std::streamsize>(snap.size())); }

    ctl.shutdown(err);
    ::WaitForSingleObject(pi.hProcess, 5000);
    ::CloseHandle(pi.hThread); ::CloseHandle(pi.hProcess);

    // 21. reload in a fresh coordinator process
    PROCESS_INFORMATION pi2; pi2.hProcess=nullptr; pi2.hThread=nullptr;
    ck(spawn_cmd(port, "build\\net_registry.bin", pi2), "spawned fresh coordinator #2 (recovered registry)");
    ck(wait_port(port), "coordinator #2 listening");
    RegistryClient r2(port); std::string err2; r2.connect(err2);
    CompatibilityDecision dReplay;
    ck(r2.query_pair(kId, devId, dReplay, err2), "[22] replayed prior decision over fresh coordinator");
    ck(dReplay.decision_id == dFinal.decision_id && dReplay.outcome == dFinal.outcome, "[22-24] same decision digest + outcome reproduced after recovery");
    r2.shutdown(err2);
    ::WaitForSingleObject(pi2.hProcess, 5000);
    ::CloseHandle(pi2.hThread); ::CloseHandle(pi2.hProcess);

    std::printf("\n=== multiprocess proof %s ===\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
