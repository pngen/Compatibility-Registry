#pragma once
#include "compat/id.hpp"
#include "compat/profile.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace compat {

// Real, observed CUDA device properties. Nothing is fabricated: fields that
// cannot be observed remain empty / false and the corresponding capabilities are
// recorded as unknown/unavailable rather than assumed.
struct CudaDeviceInfo {
    int ordinal = 0;
    std::string name;                 // e.g. "NVIDIA GeForce RTX 5090"
    std::string compute_capability;   // e.g. "12.0" (sm_120)
    std::uint64_t total_memory = 0;
    std::uint64_t shared_memory_per_block = 0;
    std::uint64_t max_threads_per_block = 0;
    std::uint64_t multi_processor_count = 0;
    std::uint64_t registers_per_multiprocessor = 0;
    std::string driver_version;       // e.g. "616.56"
    std::string runtime_version;      // e.g. "12.9"
    bool initialized = false;         // did discovery succeed?
    std::string error;
};

// Discovers real CUDA devices through the CUDA runtime/driver APIs. Returns one
// entry per device. On failure, returns a single entry with initialized=false and
// the error text (so the caller can record capabilities as unknown).
std::vector<CudaDeviceInfo> discover_cuda_devices();

// Builds a canonical DeviceProfile from observed device state. Capabilities that
// were not observed are left unset (unknown), never defaulted to false.
DeviceProfile make_cuda_device_profile(const CudaDeviceInfo& info);

} // namespace compat
