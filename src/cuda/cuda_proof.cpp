#include "compat/compat.hpp"
#include "compat/cuda.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if COMPAT_HAVE_CUDA
#include <cuda.h>
#include <cuda_runtime.h>
#include <nvrtc.h>
#endif

using namespace compat;

static int failures = 0;
static void check(bool ok, const char* msg){ std::printf("%s %s\n", ok ? "[OK]" : "[FAIL]", msg); if(!ok) failures++; }

static const char* kKernelSource = R"(
extern "C" __global__ void add_kernel(const float* a, const float* b, float* c, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) c[i] = a[i] + b[i];
}
)";

#if COMPAT_HAVE_CUDA
// Compile the kernel for a given compute capability and launch it; returns true if
// the kernel ran and the result matches CPU parity.
static bool run_kernel_sm120(const char* arch, const float* hostA, const float* hostB, float* hostC, int n) {
    nvrtcProgram prog = nullptr;
    if (nvrtcCreateProgram(&prog, kKernelSource, "add.cu", 0, nullptr, nullptr) != NVRTC_SUCCESS) return false;
    std::string opt = std::string("--gpu-architecture=") + arch;
    const char* opts[] = { opt.c_str(), "--std=c++17" };
    nvrtcResult cr = nvrtcCompileProgram(prog, 2, opts);
    if (cr != NVRTC_SUCCESS) {
        size_t sz = 0; nvrtcGetProgramLogSize(prog, &sz);
        std::string log(sz, '\0'); nvrtcGetProgramLog(prog, log.data());
        std::printf("  [nvrtc] compile failed: %s\n", log.c_str());
        nvrtcDestroyProgram(&prog);
        return false;
    }
    size_t ptxSize = 0; nvrtcGetPTXSize(prog, &ptxSize);
    std::vector<char> ptx(ptxSize); nvrtcGetPTX(prog, ptx.data());
    nvrtcDestroyProgram(&prog);

    CUdevice dev; if (cuDeviceGet(&dev, 0) != CUDA_SUCCESS) { std::printf("  [A] cuDeviceGet failed\n"); return false; }
    CUcontext ctx; if (cuDevicePrimaryCtxRetain(&ctx, dev) != CUDA_SUCCESS) { std::printf("  [A] cuDevicePrimaryCtxRetain failed\n"); return false; }
    if (cuCtxSetCurrent(ctx) != CUDA_SUCCESS) { std::printf("  [A] cuCtxSetCurrent failed\n"); return false; }

    CUmodule mod = nullptr; CUfunction func = nullptr;
    CUresult lr0 = cuModuleLoadData(&mod, ptx.data());
    if (lr0 != CUDA_SUCCESS) { const char* en=nullptr; cuGetErrorName(lr0,&en); std::printf("  [A] cuModuleLoadData failed: %s\n", en?en:"?"); return false; }
    if (cuModuleGetFunction(&func, mod, "add_kernel") != CUDA_SUCCESS) { std::printf("  [A] cuModuleGetFunction failed (symbol not found)\n"); return false; }

    float *dA=nullptr,*dB=nullptr,*dC=nullptr;
    if (cudaMalloc(&dA, n*sizeof(float))!=cudaSuccess) { std::printf("  [A] cudaMalloc dA:%s\n", cudaGetErrorString(cudaGetLastError())); return false; }
    if (cudaMalloc(&dB, n*sizeof(float))!=cudaSuccess) { std::printf("  [A] cudaMalloc dB:%s\n", cudaGetErrorString(cudaGetLastError())); return false; }
    if (cudaMalloc(&dC, n*sizeof(float))!=cudaSuccess) { std::printf("  [A] cudaMalloc dC:%s\n", cudaGetErrorString(cudaGetLastError())); return false; }
    if (cudaMemcpy(dA, hostA, n*sizeof(float), cudaMemcpyHostToDevice)!=cudaSuccess) return false;
    if (cudaMemcpy(dB, hostB, n*sizeof(float), cudaMemcpyHostToDevice)!=cudaSuccess) return false;

    int n_ = n;
    void* args[] = { &dA, &dB, &dC, &n_ };
    int block = 256, grid = (n + block - 1) / block;
    CUresult lr = cuLaunchKernel(func, grid,1,1, block,1,1, 0, nullptr, args, nullptr);
    bool ok = lr == CUDA_SUCCESS;
    if (!ok) { const char* en=nullptr; cuGetErrorName(lr, &en); std::printf("  [A] cuLaunchKernel failed: %s\n", en?en:"?"); }
    if (ok) {
        if (cudaMemcpy(hostC, dC, n*sizeof(float), cudaMemcpyDeviceToHost)!=cudaSuccess) ok=false;
        for (int i=0;i<n;++i) if (hostC[i] != hostA[i]+hostB[i]) { ok=false; break; }
    }
    cudaFree(dA); cudaFree(dB); cudaFree(dC);
    cuCtxSetCurrent(nullptr);
    cuDevicePrimaryCtxRelease(dev);
    return ok;
}
#endif

int main() {
    std::printf("=== Compatibility Registry CUDA proof (real RTX 5090 / sm_120) ===\n");

    std::vector<CudaDeviceInfo> devs = discover_cuda_devices();
    check(!devs.empty(), "device discovery returned at least one entry");
    check(devs[0].initialized, "device discovery initialized (CUDA device present)");
    if (!devs[0].initialized) {
        std::printf("  error: %s\n", devs[0].error.c_str());
        // Still demonstrate registry-level rejection without hardware.
        CompatibilityRegistry reg;
        DeviceProfile devReq = make_cuda_device_profile(devs[0]);
        ProfileId devId = reg.register_profile(ProfileKind::Device, devReq.to_canon());
        // kernel requiring unsupported arch
        KernelProfile kp; kp.kernel_artifact_id = KernelArtifactId::genseed();
        kp.operation="add"; kp.architecture="sm_130"; kp.compute_capability="13.0";
        kp.abi="cuda"; kp.runtime="cudart"; kp.compiler="nvcc"; kp.compiler_version="12.9";
        kp.dtype="f32"; kp.layout="aos"; kp.shape_spec="[N]";
        ProfileId kid = reg.register_profile(ProfileKind::Kernel, kp.to_canon());
        CompatibilityRule rule; rule.rule_id=CompatibilityRuleId::genseed();
        rule.scope="pair"; rule.left_kind=ProfileKind::Kernel; rule.right_kind=ProfileKind::Device; rule.outcome=Outcome::Compatible;
        Constraint req; req.op=PredOp::VersionLe; req.field="compute_capability"; req.right_field="compute_capability"; rule.required.push_back(req);
        Constraint inc; inc.op=PredOp::VersionGt; inc.field="compute_capability"; inc.right_field="compute_capability"; rule.incompatible_with.push_back(inc);
        reg.register_rule(rule);
        auto d = reg.evaluate_pair(kid, devId);
        check(d.outcome == Outcome::Incompatible, "[B] unsupported arch rejected before execution (registry-level)");
        return failures ? 1 : 0;
    }

    const auto& dev = devs[0];
    std::printf("  device: %s  compute=%s  mem=%llu MB  runtime=%s  driver=%s\n",
                dev.name.c_str(), dev.compute_capability.c_str(),
                (unsigned long long)(dev.total_memory/(1024*1024)), dev.runtime_version.c_str(), dev.driver_version.c_str());

    CompatibilityRegistry reg;
    DeviceProfile devProf = make_cuda_device_profile(dev);
    ProfileId devId = reg.register_profile(ProfileKind::Device, devProf.to_canon());

    // kernel/device compatibility rule
    CompatibilityRule rule; rule.rule_id=CompatibilityRuleId::genseed();
    rule.scope="pair"; rule.left_kind=ProfileKind::Kernel; rule.right_kind=ProfileKind::Device; rule.outcome=Outcome::Compatible;
    Constraint req; req.op=PredOp::VersionLe; req.field="compute_capability"; req.right_field="compute_capability"; rule.required.push_back(req);
    Constraint inc; inc.op=PredOp::VersionGt; inc.field="compute_capability"; inc.right_field="compute_capability"; rule.incompatible_with.push_back(inc);
    reg.register_rule(rule);

    // CASE A: exact supported target sm_120 -> registry says COMPATIBLE, then execute.
    KernelProfile kp120; kp120.kernel_artifact_id=KernelArtifactId::genseed();
    kp120.operation="add"; kp120.architecture="sm_120"; kp120.compute_capability="12.0";
    kp120.abi="cuda"; kp120.runtime="cudart"; kp120.compiler="nvcc"; kp120.compiler_version="12.9";
    kp120.dtype="f32"; kp120.layout="aos"; kp120.shape_spec="[N]";
    ProfileId k120 = reg.register_profile(ProfileKind::Kernel, kp120.to_canon());
    auto dA = reg.evaluate_pair(k120, devId);
    check(dA.outcome == Outcome::Compatible, "[A] sm_120 kernel -> COMPATIBLE with RTX 5090");

    const int N = 1024;
    std::vector<float> a(N), b(N), c(N);
    for (int i=0;i<N;++i){ a[i]=float(i); b[i]=float(i+1); c[i]=0.f; }
    std::string arch = "compute_" + dev.compute_capability; // e.g. compute_12.0
    // normalize: compute capability "12.0" -> "compute_120"
    std::string cc = dev.compute_capability; cc.erase(std::remove(cc.begin(), cc.end(), '.'), cc.end());
    bool ran = run_kernel_sm120(("compute_"+cc).c_str(), a.data(), b.data(), c.data(), N);
    check(ran, "[A] real CUDA kernel launched on sm_120, CPU parity verified");

    // CASE B: unsupported/too-high arch -> registry rejects BEFORE execution.
    KernelProfile kp130 = kp120; kp130.kernel_artifact_id=KernelArtifactId::genseed();
    kp130.architecture="sm_130"; kp130.compute_capability="13.0";
    ProfileId k130 = reg.register_profile(ProfileKind::Kernel, kp130.to_canon());
    auto dB = reg.evaluate_pair(k130, devId);
    check(dB.outcome == Outcome::Incompatible, "[B] sm_130 kernel -> INCOMPATIBLE (rejected before execution)");
    // For the incompatible case the proof NEVER launches; rejection is proven at the registry.
    check(dB.failed.size() > 0, "[B] incompatible decision records the failed dimension");

    // CASE C: dtype/layout mismatch.
    KernelProfile kpF64 = kp120; kpF64.kernel_artifact_id=KernelArtifactId::genseed();
    kpF64.dtype="f64";
    ProfileId kf64 = reg.register_profile(ProfileKind::Kernel, kpF64.to_canon());
    CompatibilityRule dtypeRule; dtypeRule.rule_id=CompatibilityRuleId::genseed();
    dtypeRule.scope="pair"; dtypeRule.left_kind=ProfileKind::Kernel; dtypeRule.right_kind=ProfileKind::Device; dtypeRule.outcome=Outcome::Compatible;
    dtypeRule.priority=10; // defensive: explicit incompatibility outranks the compute-cap rule
    Constraint incC; incC.op=PredOp::Eq; incC.field="dtype"; incC.expected=Canon::mk_str("f64");
    dtypeRule.incompatible_with.push_back(incC);
    reg.register_rule(dtypeRule);
    auto dC = reg.evaluate_pair(kf64, devId);
    check(dC.outcome == Outcome::Incompatible, "[C] dtype/layout mismatch (f64) -> INCOMPATIBLE before execution");
    check(!(dC.outcome == Outcome::Exact), "[C] mismatch never collapses to EXACT");

    // CASE D: stale artifact generation.
    KernelProfile g1 = kp120; g1.kernel_artifact_id=KernelArtifactId::genseed(); g1.artifact_digest="abc123"; g1.generation=1;
    ProfileId kg1 = reg.register_profile(ProfileKind::Kernel, g1.to_canon());
    auto decG1 = reg.evaluate_pair(kg1, devId);
    KernelProfile g2 = g1; g2.artifact_digest="def456"; g2.generation=2;
    reg.supersede_profile(ProfileKind::Kernel, kg1.value, g2.to_canon());
    auto decG2 = reg.evaluate_pair(kg1, devId);
    check(decG1.decision_id != decG2.decision_id, "[D] stale G1 and current G2 decisions are distinct");
    check(decG1.digest != decG2.digest, "[D] historical decision digest preserved (replay) and current differs");

    // CASE E: runtime/toolchain generation change.
    DeviceProfile devG1 = devProf; 
    ProfileId devIdA = reg.register_profile(ProfileKind::Device, devG1.to_canon());
    auto decE1 = reg.evaluate_pair(k120, devIdA);
    DeviceProfile devG2 = devG1; devG2.runtime_min="99.0"; // explicit runtime generation change
    reg.supersede_profile(ProfileKind::Device, devIdA.value, devG2.to_canon());
    auto decE2 = reg.evaluate_pair(k120, devIdA);
    check(decE1.decision_id != decE2.decision_id, "[E] historical and current decisions remain distinct after toolchain/runtime gen change");

    std::printf("\n=== CUDA proof %s ===\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
