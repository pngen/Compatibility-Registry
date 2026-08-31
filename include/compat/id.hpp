#pragma once
#include "compat/uuid.hpp"
#include <compare>
#include <cstdint>
#include <string>

namespace compat {

// A monotonic, comparable generation counter. Generations are never rewritten;
// a new generation is a new value.
struct Generation {
    std::uint64_t value = 0;
};

inline constexpr bool operator==(const Generation& a, const Generation& b) noexcept { return a.value == b.value; }
inline constexpr bool operator!=(const Generation& a, const Generation& b) noexcept { return a.value != b.value; }
inline constexpr bool operator<(const Generation& a, const Generation& b) noexcept { return a.value < b.value; }
inline constexpr bool operator>(const Generation& a, const Generation& b) noexcept { return a.value > b.value; }
inline constexpr bool operator<=(const Generation& a, const Generation& b) noexcept { return a.value <= b.value; }
inline constexpr bool operator>=(const Generation& a, const Generation& b) noexcept { return a.value >= b.value; }

#define COMPAT_DECLARE_ID(NAME) \
    struct NAME { \
        Uuid value; \
        NAME() noexcept {} \
        explicit NAME(Uuid v) noexcept : value(v) {} \
        static NAME from_string(std::string_view s) { return NAME{Uuid::from_string(s)}; } \
        std::string to_string() const { return value.to_string(); } \
        bool is_zero() const noexcept { return value.is_zero(); } \
        bool operator==(const NAME& o) const noexcept { return value == o.value; } \
        bool operator!=(const NAME& o) const noexcept { return value != o.value; } \
        bool operator<(const NAME& o) const noexcept { return value < o.value; } \
        std::size_t hash() const noexcept { return value.hash(); } \
        static NAME genseed() { return NAME{Uuid::genseed()}; } \
        operator Uuid() const noexcept { return value; } \
    };

// Strongly-typed 128-bit identities.
COMPAT_DECLARE_ID(CompatibilityIdentityId)
COMPAT_DECLARE_ID(CompatibilityRecordId)
COMPAT_DECLARE_ID(CompatibilityRuleId)
COMPAT_DECLARE_ID(CompatibilityDecisionId)
COMPAT_DECLARE_ID(EvidenceId)
COMPAT_DECLARE_ID(ModelId)
COMPAT_DECLARE_ID(ModelRevisionId)
COMPAT_DECLARE_ID(TokenizerId)
COMPAT_DECLARE_ID(VocabularyId)
COMPAT_DECLARE_ID(AdapterId)
COMPAT_DECLARE_ID(TensorSchemaId)
COMPAT_DECLARE_ID(KVFormatId)
COMPAT_DECLARE_ID(KernelArtifactId)
COMPAT_DECLARE_ID(GraphArtifactId)
COMPAT_DECLARE_ID(CompilationArtifactId)
COMPAT_DECLARE_ID(BackendId)
COMPAT_DECLARE_ID(ToolchainId)
COMPAT_DECLARE_ID(DriverId)
COMPAT_DECLARE_ID(RuntimeId)
COMPAT_DECLARE_ID(ProtocolId)
COMPAT_DECLARE_ID(InterfaceId)
COMPAT_DECLARE_ID(DeviceId)
COMPAT_DECLARE_ID(ArchitectureId)
COMPAT_DECLARE_ID(CapabilitySetId)
COMPAT_DECLARE_ID(PolicyId)
COMPAT_DECLARE_ID(ProfileId)

// Distinct generation kinds.
struct RegistryGeneration { Generation g; };
struct RuleGeneration { Generation g; };
struct EvidenceGeneration { Generation g; };
struct CapabilityGeneration { Generation g; };
struct PolicyGeneration { Generation g; };

} // namespace compat

namespace std {
template <> struct hash<compat::ProfileId> { std::size_t operator()(const compat::ProfileId& p) const noexcept { return p.hash(); } };
}
