#include "compat/uuid.hpp"
#include <random>

namespace compat {
Uuid Uuid::genseed() noexcept {
    std::random_device rd;
    std::uint64_t a = (static_cast<std::uint64_t>(rd()) << 32) ^ static_cast<std::uint64_t>(rd());
    std::uint64_t b = (static_cast<std::uint64_t>(rd()) << 32) ^ static_cast<std::uint64_t>(rd());
    return Uuid::generate_v4(a, b);
}
} // namespace compat
