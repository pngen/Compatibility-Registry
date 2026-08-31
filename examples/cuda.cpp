#include "compat/cuda.hpp"
#include "compat/compat.hpp"
#include <cstdio>
using namespace compat;
// Real CUDA device discovery against the RTX 5090 / sm_120.
int main() {
    auto devs = discover_cuda_devices();
    if (devs.empty() || !devs[0].initialized) {
        std::printf("no CUDA device observed: %s\n", devs.empty() ? "none" : devs[0].error.c_str());
        return 0;
    }
    const auto& d = devs[0];
    std::printf("CUDA device: %s\n", d.name.c_str());
    std::printf("  compute capability: %s  total mem: %llu MB  SMs: %llu\n",
                d.compute_capability.c_str(), (unsigned long long)(d.total_memory/(1024*1024)),
                (unsigned long long)d.multi_processor_count);
    DeviceProfile p = make_cuda_device_profile(d);
    std::printf("  canonical device profile fp: %s\n", canonical_fingerprint_hex(p.to_canon()).c_str());
    // Only observed capabilities are recorded; nothing is fabricated.
    if (p.compute_capability == "12.0") std::printf("  architecture is sm_120 (RTX 5090)\n");
    return 0;
}
