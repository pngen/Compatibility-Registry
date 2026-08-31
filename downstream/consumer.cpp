// Downstream consumer proof for the Compatibility Registry 1.0.0 C++20 library.
// Uses find_package(CompatibilityRegistry CONFIG REQUIRED) + CompatibilityRegistry::compat.
#include "compat/compat.hpp"

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

using namespace compat;

int main() {
    try {
        CompatibilityRegistry reg;

        // Device profile: compute_capability "12.0".
        DeviceId dev120 = DeviceId::genseed();
        DeviceProfile dev;
        dev.device_id = dev120;
        dev.vendor = "NVIDIA";
        dev.architecture = "sm_12.0";
        dev.compute_capability = "12.0";
        dev.memory_model = "global";
        dev.total_memory = 32607ull * 1024ull * 1024ull;
        dev.supported_dtypes = {"f16", "f32", "bf16"};
        dev.instruction_classes = {"sm_120"};
        ProfileId devId = reg.register_profile(ProfileKind::Device, dev.to_canon());
        if (devId.is_zero()) { std::printf("FAIL: device profile id is zero\n"); return 1; }

        // Kernel profile: compute_capability "12.0".
        KernelArtifactId k120 = KernelArtifactId::genseed();
        KernelProfile kern;
        kern.kernel_artifact_id = k120;
        kern.operation = "add";
        kern.architecture = "sm_12.0";
        kern.compute_capability = "12.0";
        kern.abi = "cuda";
        kern.runtime = "cudart";
        kern.compiler = "nvcc";
        kern.compiler_version = "12.9";
        kern.dtype = "f32";
        kern.layout = "aos";
        kern.shape_spec = "[N]";
        ProfileId kernId = reg.register_profile(ProfileKind::Kernel, kern.to_canon());
        if (kernId.is_zero()) { std::printf("FAIL: kernel profile id is zero\n"); return 1; }

        // Compatibility rule: kernel <= device for compute_capability, and a
        // kernel strictly greater than the device is incompatible.
        CompatibilityRule rule;
        rule.rule_id = CompatibilityRuleId::genseed();
        rule.domain = "kernel-device";
        rule.scope = "pair";
        rule.left_kind = ProfileKind::Kernel;
        rule.right_kind = ProfileKind::Device;
        rule.kind = RuleKind::Equality;
        rule.outcome = Outcome::Compatible;
        Constraint req;
        req.op = PredOp::VersionLe;
        req.field = "compute_capability";
        req.right_field = "compute_capability";
        rule.required.push_back(req);
        Constraint inc;
        inc.op = PredOp::VersionGt;
        inc.field = "compute_capability";
        inc.right_field = "compute_capability";
        rule.incompatible_with.push_back(inc);
        reg.register_rule(rule);

        // Evaluate the kernel/device pair and require COMPATIBLE.
        CompatibilityDecision before = reg.evaluate_pair(kernId, devId);
        if (before.outcome != Outcome::Compatible) {
            std::printf("FAIL: expected COMPATIBLE, got %s\n", outcome_name(before.outcome));
            return 1;
        }

        // Save + recover, then assert the decision digest is preserved.
        std::vector<std::uint8_t> snap = reg.snapshot();
        auto reg2 = CompatibilityRegistry::recover(snap);
        if (!reg2) { std::printf("FAIL: recover returned null\n"); return 1; }

        CompatibilityDecision after = reg2->evaluate_pair(kernId, devId);
        if (after.outcome != Outcome::Compatible) {
            std::printf("FAIL: after recover expected COMPATIBLE, got %s\n", outcome_name(after.outcome));
            return 1;
        }
        if (!(after.digest == before.digest)) {
            std::printf("FAIL: decision digest not preserved after recover\n");
            return 1;
        }
        if (!(after.decision_id == before.decision_id)) {
            std::printf("FAIL: decision id not preserved after recover\n");
            return 1;
        }

        std::printf("consumer OK\n");
        return 0;
    } catch (const std::exception& e) {
        std::printf("FAIL: exception: %s\n", e.what());
        return 1;
    }
}
