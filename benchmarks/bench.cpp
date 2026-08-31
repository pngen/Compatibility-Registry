#include "compat/compat.hpp"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace compat;

static double ms(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static void build_profiles(std::vector<ProfileId>& devs, std::vector<ProfileId>& kerns, CompatibilityRegistry& reg, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        DeviceProfile d; d.device_id=DeviceId::genseed(); d.vendor="NVIDIA";
        d.compute_capability="12.0"; d.architecture="sm_120"; d.memory_model="global";
        d.total_memory=32579ull*1024ull*1024ull; d.supported_dtypes={"f16","f32","bf16"};
        d.instruction_classes={"sm_120"};
        devs.push_back(reg.register_profile(ProfileKind::Device, d.to_canon()));
        KernelProfile k; k.kernel_artifact_id=KernelArtifactId::genseed(); k.operation="add";
        k.architecture="sm_120"; k.compute_capability="12.0"; k.abi="cuda"; k.runtime="cudart";
        k.compiler="nvcc"; k.compiler_version="12.9"; k.dtype="f32"; k.layout="aos"; k.shape_spec="[N]";
        kerns.push_back(reg.register_profile(ProfileKind::Kernel, k.to_canon()));
    }
}

int main(int argc, char** argv) {
    std::size_t n = argc > 1 ? static_cast<std::size_t>(std::atoi(argv[1])) : 1000;
    std::printf("Compatibility Registry benchmark at N=%zu profiles\n", n);

    // profile construction + fingerprint
    auto t0 = std::chrono::steady_clock::now();
    CompatibilityRegistry reg;
    std::vector<ProfileId> devs, kerns;
    build_profiles(devs, kerns, reg, n);
    auto t1 = std::chrono::steady_clock::now();
    std::printf("  register %zu profiles (incl SHA-256 fingerprint): %8.3f ms\n", 2*n, ms(t0, t1));

    // exact identity lookup
    t0 = std::chrono::steady_clock::now();
    std::size_t found = 0;
    for (std::size_t i = 0; i < n; ++i) if (reg.find_profile(devs[i].value)) found++;
    t1 = std::chrono::steady_clock::now();
    std::printf("  %zu exact identity lookups:                       %8.3f ms (%.2f us/op)\n", n, ms(t0,t1), ms(t0,t1)*1000.0/n);

    // pairwise evaluation
    CompatibilityRule rule; rule.rule_id=CompatibilityRuleId::genseed(); rule.scope="pair";
    rule.left_kind=ProfileKind::Kernel; rule.right_kind=ProfileKind::Device; rule.outcome=Outcome::Compatible;
    Constraint req; req.op=PredOp::VersionLe; req.field="compute_capability"; req.right_field="compute_capability"; rule.required.push_back(req);
    Constraint inc; inc.op=PredOp::VersionGt; inc.field="compute_capability"; inc.right_field="compute_capability"; rule.incompatible_with.push_back(inc);
    reg.register_rule(rule);
    t0 = std::chrono::steady_clock::now();
    std::size_t k = 0;
    for (std::size_t i = 0; i < n; ++i) { auto d = reg.evaluate_pair(kerns[i], devs[i]); if (d.outcome==Outcome::Compatible) k++; }
    t1 = std::chrono::steady_clock::now();
    std::printf("  %zu pairwise evaluations:                         %8.3f ms (%.2f us/op)\n", n, ms(t0,t1), ms(t0,t1)*1000.0/n);

    // requirement matching
    t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < n; ++i) { auto d = reg.evaluate_requirement(kerns[i], devs[i]); (void)d; }
    t1 = std::chrono::steady_clock::now();
    std::printf("  %zu requirement matches:                          %8.3f ms\n", n, ms(t0,t1));

    // matrix (1k*kernel x device) - use a bounded sample to avoid O(N^2) blowup
    std::size_t m = n > 300 ? 300 : n;
    t0 = std::chrono::steady_clock::now();
    MatrixResult mr = reg.matrix(std::vector<Uuid>(kerns.begin(), kerns.begin()+m), std::vector<Uuid>(devs.begin(), devs.begin()+m));
    t1 = std::chrono::steady_clock::now();
    std::printf("  %zu x %zu matrix (%zu cells):                  %8.3f ms\n", m, m, mr.cells.size(), ms(t0,t1));

    // explanation generation
    auto d = reg.evaluate_pair(kerns[0], devs[0]);
    t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < std::min<std::size_t>(1000, n); ++i) { volatile std::string s = reg.explain_text(d); (void)s; }
    t1 = std::chrono::steady_clock::now();
    std::printf("  %zu explanation generations:                    %8.3f ms\n", std::min<std::size_t>(1000,n), ms(t0,t1));

    // persistence save + recovery
    t0 = std::chrono::steady_clock::now();
    std::vector<std::uint8_t> snap = reg.snapshot();
    auto reg2 = CompatibilityRegistry::recover(snap);
    t1 = std::chrono::steady_clock::now();
    std::printf("  snapshot+recover (%.0f KB):                       %8.3f ms\n", double(snap.size())/1024.0, ms(t0,t1));

    // historical replay
    CompatibilityDecision c1 = reg.evaluate_pair(kerns[0], devs[0]);
    t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < std::min<std::size_t>(1000, n); ++i) { auto rp = reg2->evaluate_pair(kerns[0], devs[0]); (void)rp; }
    t1 = std::chrono::steady_clock::now();
    std::printf("  %zu replayed decisions:                          %8.3f ms\n", std::min<std::size_t>(1000,n), ms(t0,t1));

    // dependency invalidation (build a dependency chain then invalidate the root)
    CompatibilityRegistry dreg;
    std::vector<ProfileId> chain;
    ProfileId prev = dreg.register_profile(ProfileKind::Model, Canon::mk_record(Canon::rec()), {}, "");
    for (std::size_t i = 1; i < std::min<std::size_t>(1000, n); ++i) chain.push_back(dreg.register_profile(ProfileKind::Model, Canon::mk_record(Canon::rec()), {prev.value}, ""));
    t0 = std::chrono::steady_clock::now();
    dreg.propagate_invalidation(prev.value);
    t1 = std::chrono::steady_clock::now();
    std::printf("  dependency invalidation over %zu nodes:         %8.3f ms\n", chain.size(), ms(t0,t1));

    (void)c1;
    std::printf("  registry size: %zu profiles, %zu rules, %zu evidence\n", reg.stats().profiles, reg.stats().rules, reg.stats().evidence);
    return 0;
}
