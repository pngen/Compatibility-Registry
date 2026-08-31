
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <algorithm>
#include <stdexcept>

namespace compat {

// A strong 128-bit identifier. Stored as 16 raw bytes and treated as opaque.
// Serialization is deterministic and round-trips exactly.
class Uuid {
public:
    static constexpr std::size_t kSize = 16;
    using Bytes = std::array<unsigned char, kSize>;

    Uuid() noexcept { bytes_.fill(0); }
    explicit Uuid(const Bytes& b) noexcept : bytes_(b) {}

    static Uuid from_bytes(const Bytes& b) noexcept { return Uuid(b); }

    // Accepts 32 hex digits (no dashes) or canonical 8-4-4-4-12 form.
    static Uuid from_string(std::string_view s) {
        Uuid u;
        std::string hex;
        hex.reserve(32);
        for (char ch : s) {
            if (ch == '-') continue;
            hex.push_back(ch);
        }
        if (hex.size() != 32) {
            throw std::invalid_argument("Uuid::from_string: invalid length");
        }
        for (std::size_t i = 0; i < 32; i += 2) {
            u.bytes_[i / 2] = static_cast<unsigned char>((hexv(hex[i]) << 4) | hexv(hex[i + 1]));
        }
        return u;
    }

    // RFC-4122 v4 style (random 122 bits + version/variant bits) from a seed.
    static Uuid generate_v4(std::uint64_t a, std::uint64_t b) noexcept {
        Bytes by{};
        std::uint64_t words[2] = { a, b };
        std::memcpy(by.data(), words, sizeof(words));
        by[6] = static_cast<unsigned char>((by[6] & 0x0Fu) | 0x40u); // version 4
        by[8] = static_cast<unsigned char>((by[8] & 0x3Fu) | 0x80u); // variant 10
        return Uuid(by);
    }

    static Uuid genseed() noexcept;   // defined in compat.cpp (thread-safe source)

    const Bytes& bytes() const noexcept { return bytes_; }
    bool is_zero() const noexcept { return std::all_of(bytes_.begin(), bytes_.end(), [](unsigned char c) { return c == 0; }); }

    std::string to_string() const {
        std::string out;
        out.reserve(36);
        for (std::size_t i = 0; i < kSize; ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
            out.push_back(hexl(bytes_[i] >> 4));
            out.push_back(hexl(bytes_[i] & 0x0Fu));
        }
        return out;
    }

    bool operator==(const Uuid& o) const noexcept { return bytes_ == o.bytes_; }
    bool operator!=(const Uuid& o) const noexcept { return bytes_ != o.bytes_; }
    bool operator<(const Uuid& o) const noexcept { return bytes_ < o.bytes_; }
    bool operator>(const Uuid& o) const noexcept { return bytes_ > o.bytes_; }
    bool operator<=(const Uuid& o) const noexcept { return bytes_ <= o.bytes_; }
    bool operator>=(const Uuid& o) const noexcept { return bytes_ >= o.bytes_; }

    std::size_t hash() const noexcept {
        // FNV-1a over the 16 bytes.
        std::uint64_t h = 14695981039346656037ull;
        for (unsigned char b : bytes_) {
            h ^= static_cast<std::uint64_t>(b);
            h *= 1099511628211ull;
        }
        return static_cast<std::size_t>(h);
    }

private:
    Bytes bytes_{};
    static int hexv(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw std::invalid_argument("Uuid::from_string: non-hex character");
    }
    static char hexl(unsigned char nib) { return static_cast<char>(nib < 10 ? ('0' + nib) : ('a' + (nib - 10))); }
};

} // namespace compat

namespace std {
template <> struct hash<compat::Uuid> {
    std::size_t operator()(const compat::Uuid& u) const noexcept { return u.hash(); }
};
} // namespace std
