#define COMPAT_INCLUDE_NET
#include "compat/compat.hpp"
#include "compat/cuda.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace compat;

static std::unique_ptr<CompatibilityRegistry> build_demo() {
    auto reg = std::make_unique<CompatibilityRegistry>();
    // device profiles
    DeviceProfile d120; d120.device_id=DeviceId::genseed(); d120.vendor="NVIDIA";
    d120.architecture="sm_120"; d120.compute_capability="12.0"; d120.memory_model="global";
    d120.total_memory=32579ull*1024ull*1024ull;
    d120.supported_dtypes={"f16","f32","bf16"}; d120.instruction_classes={"sm_120"};
    ProfileId d120id = reg->register_profile(ProfileKind::Device, d120.to_canon());
    DeviceProfile d90 = d120; d90.device_id=DeviceId::genseed(); d90.architecture="sm_90"; d90.compute_capability="9.0"; d90.total_memory=0;
    ProfileId d90id = reg->register_profile(ProfileKind::Device, d90.to_canon());
    // kernel profiles
    KernelProfile k120; k120.kernel_artifact_id=KernelArtifactId::genseed(); k120.operation="add";
    k120.architecture="sm_120"; k120.compute_capability="12.0"; k120.abi="cuda"; k120.runtime="cudart";
    k120.compiler="nvcc"; k120.compiler_version="12.9"; k120.dtype="f32"; k120.layout="aos"; k120.shape_spec="[N]";
    ProfileId k120id = reg->register_profile(ProfileKind::Kernel, k120.to_canon());
    KernelProfile k130 = k120; k130.kernel_artifact_id=KernelArtifactId::genseed(); k130.compute_capability="13.0"; k130.architecture="sm_130";
    ProfileId k130id = reg->register_profile(ProfileKind::Kernel, k130.to_canon());
    // rule
    CompatibilityRule rule; rule.rule_id=CompatibilityRuleId::genseed(); rule.scope="pair";
    rule.left_kind=ProfileKind::Kernel; rule.right_kind=ProfileKind::Device; rule.outcome=Outcome::Compatible;
    Constraint req; req.op=PredOp::VersionLe; req.field="compute_capability"; req.right_field="compute_capability";
    Constraint inc; inc.op=PredOp::VersionGt; inc.field="compute_capability"; inc.right_field="compute_capability";
    rule.required.push_back(req); rule.incompatible_with.push_back(inc);
    reg->register_rule(rule);
    (void)d120id; (void)d90id; (void)k130id;
    return reg;
}

static void print_decision(const CompatibilityDecision& d) {
    std::printf("  outcome: %s\n", outcome_name(d.outcome));
    std::printf("  left: %s  right: %s\n", d.left.to_string().c_str(), d.right.to_string().c_str());
    if (!d.rule_id.is_zero()) std::printf("  rule: %s (gen %llu)\n", d.rule_id.to_string().c_str(), (unsigned long long)d.rule_generation.value);
    std::printf("  digest: %s\n", d.digest_hex().c_str());
    std::printf("  explanation:\n%s\n", d.explanation.c_str());
}

int main(int argc, char** argv) {
    std::string cmd = argc > 1 ? argv[1] : "help";
    if (cmd == "version") {
        std::printf("Compatibility Registry 1.0.0 (Summon Software Labs)\nApache License 2.0. No telemetry transmission.\n");
        return 0;
    }
    if (cmd == "device") {
        std::vector<CudaDeviceInfo> devs = discover_cuda_devices();
        if (devs.empty() || !devs[0].initialized) {
            std::printf("CUDA device discovery: %s\n", devs.empty()? "none" : devs[0].error.c_str());
            return 0;
        }
        for (const auto& d : devs) {
            std::printf("device %d: %s\n", d.ordinal, d.name.c_str());
            std::printf("  compute capability: %s\n", d.compute_capability.c_str());
            std::printf("  total memory: %llu MB\n", (unsigned long long)(d.total_memory/(1024*1024)));
            std::printf("  shared mem/block: %llu  max threads/block: %llu  SMs: %llu\n",
                        (unsigned long long)d.shared_memory_per_block, (unsigned long long)d.max_threads_per_block,
                        (unsigned long long)d.multi_processor_count);
            std::printf("  runtime: %s  driver: %s\n", d.runtime_version.c_str(), d.driver_version.c_str());
            DeviceProfile p = make_cuda_device_profile(d);
            std::printf("  canonical profile fp: %s\n", canonical_fingerprint_hex(p.to_canon()).c_str());
        }
        return 0;
    }
    if (cmd == "demo") {
        auto reg = build_demo();
        auto by = reg->query_by_kind(ProfileKind::Device);
        auto ks = reg->query_by_kind(ProfileKind::Kernel);
        Uuid d120 = by[0].id, k120 = ks[0].id, k130 = ks[1].id;
        std::printf("=== pair (kernel 12.0 vs device 12.0) ===\n");
        auto c1 = reg->evaluate_pair(k120, d120);
        print_decision(c1);
        std::printf("=== pair (kernel 13.0 vs device 12.0) ===\n");
        auto c2 = reg->evaluate_pair(k130, d120);
        print_decision(c2);
        std::printf("=== matrix 2x2 ===\n");
        auto mr = reg->matrix({k120, k130}, {by[0].id, by[1].id});
        std::printf("  compatible=%zu incompatible=%zu cells=%zu\n", mr.compatible, mr.incompatible, mr.cells.size());
        std::printf("=== snapshot/save/recover/replay ===\n");
        std::vector<std::uint8_t> snap = reg->snapshot();
        reg->save("build\\cli_registry.bin");
        auto reg2 = CompatibilityRegistry::load("build\\cli_registry.bin");
        auto rp = reg2->evaluate_pair(k120, d120);
        std::printf("  replay digest equals? %s\n", rp.digest == c1.digest ? "YES" : "NO");
        std::printf("  loaded profiles: %zu\n", reg2->stats().profiles);
        std::printf("  snapshot bytes: %zu\n", snap.size());
        std::printf("=== counterfactual (device 12.0 -> 9.0) ===\n");
        auto cf = reg->counterfactual_pair(k120, d120, {{"compute_capability", Canon::mk_str("9.0")}});
        std::printf("  counterfactual outcome: %s (derived, never authoritative)\n", outcome_name(cf.outcome));
        std::printf("=== history / dependencies ===\n");
        std::printf("  dependencies of kernel: %zu\n", reg->dependencies_of(k120).size());
        std::printf("  registry generation: %llu\n", (unsigned long long)reg->current_registry_generation().g.value);
        return 0;
    }
    if (cmd == "help") {
        std::printf("compat <command>\n\nCommands:\n");
        std::printf("  version        print version + license\n");
        std::printf("  device         discover real CUDA device(s) and canonical device profile\n");
        std::printf("  demo           run an in-process compatibility demonstration\n");
        std::printf("  cuda           (see compat_cuda_proof for the real CUDA compatibility proof)\n");
        std::printf("  multiprocess   (see compat_net_proof for the real coordinator + TCP proof)\n");
        return 0;
    }
    std::printf("unknown command '%s'\n", cmd.c_str());
    return 1;
}
