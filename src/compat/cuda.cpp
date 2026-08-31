#include "compat/cuda.hpp"

#if COMPAT_HAVE_CUDA
#include <cuda.h>
#include <cuda_runtime.h>
#include <cstring>
#endif

#include <sstream>

namespace compat {

#if !COMPAT_HAVE_CUDA
std::vector<CudaDeviceInfo> discover_cuda_devices() {
    CudaDeviceInfo d;
    d.initialized = false;
    d.error = "CUDA support not compiled in";
    return {std::move(d)};
}
DeviceProfile make_cuda_device_profile(const CudaDeviceInfo& info) {
    DeviceProfile p;
    p.device_id = DeviceId::genseed();
    p.vendor = "NVIDIA";
    p.architecture = info.initialized ? ("sm_" + info.compute_capability) : "";
    p.compute_capability = info.initialized ? info.compute_capability : "";
    p.memory_model = "global";
    p.runtime_min = info.initialized ? info.runtime_version : "";
    p.driver_min = info.initialized ? info.driver_version : "";
    if (info.initialized) {
        p.supported_dtypes = {"f16", "f32", "bf16"};
        p.instruction_classes = {"sm_" + info.compute_capability};
        p.total_memory = info.total_memory;
    }
    return p;
}
#else

static std::string format_cuda_version(int v) {
    int major = v / 1000;
    int minor = v % 1000;
    std::ostringstream ss; ss << major << "." << minor;
    return ss.str();
}

std::vector<CudaDeviceInfo> discover_cuda_devices() {
    std::vector<CudaDeviceInfo> out;
    int count = 0;
    cudaError_t rc = cudaGetDeviceCount(&count);
    if (rc != cudaSuccess) {
        CudaDeviceInfo d;
        d.initialized = false;
        d.error = std::string("cudaGetDeviceCount failed: ") + cudaGetErrorString(rc);
        out.push_back(std::move(d));
        return out;
    }
    if (count <= 0) {
        CudaDeviceInfo d; d.initialized = false; d.error = "no CUDA device"; out.push_back(std::move(d)); return out;
    }
    int driverVer = 0, runtimeVer = 0;
    if (cudaDriverGetVersion(&driverVer) != cudaSuccess) driverVer = 0;
    if (cudaRuntimeGetVersion(&runtimeVer) != cudaSuccess) runtimeVer = 0;
    for (int i = 0; i < count; ++i) {
        cudaDeviceProp prop;
        CudaDeviceInfo d;
        d.ordinal = i;
        if (cudaGetDeviceProperties(&prop, i) != cudaSuccess) {
            d.initialized = false;
            d.error = "cudaGetDeviceProperties failed for device " + std::to_string(i);
        } else {
            d.initialized = true;
            d.name = prop.name;
            std::ostringstream cc; cc << prop.major << "." << prop.minor; d.compute_capability = cc.str();
            d.total_memory = static_cast<std::uint64_t>(prop.totalGlobalMem);
            d.shared_memory_per_block = static_cast<std::uint64_t>(prop.sharedMemPerBlock);
            d.max_threads_per_block = static_cast<std::uint64_t>(prop.maxThreadsPerBlock);
            d.multi_processor_count = static_cast<std::uint64_t>(prop.multiProcessorCount);
            d.registers_per_multiprocessor = static_cast<std::uint64_t>(prop.regsPerMultiprocessor);
            d.driver_version = driverVer ? format_cuda_version(driverVer) : "";
            d.runtime_version = runtimeVer ? format_cuda_version(runtimeVer) : "";
        }
        out.push_back(std::move(d));
    }
    return out;
}

DeviceProfile make_cuda_device_profile(const CudaDeviceInfo& info) {
    DeviceProfile p;
    p.device_id = DeviceId::genseed();
    p.vendor = "NVIDIA";
    p.architecture = info.initialized ? ("sm_" + info.compute_capability) : "";
    p.compute_capability = info.initialized ? info.compute_capability : "";
    p.memory_model = "global";
    p.runtime_min = info.initialized ? info.runtime_version : "";
    p.driver_min = info.initialized ? info.driver_version : "";
    if (info.initialized) {
        p.supported_dtypes = {"f16", "f32", "bf16"};
        p.instruction_classes = {"sm_" + info.compute_capability};
        p.total_memory = info.total_memory;
    }
    return p;
}

#endif // COMPAT_HAVE_CUDA

} // namespace compat
