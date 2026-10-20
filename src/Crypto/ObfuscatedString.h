#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace octarine::crypto {

// Compile-time string obfuscation wrapper. Encrypts string literals in constexpr evaluation
// so plaintext secrets do not appear in the binary's .rodata section, and decrypts on the stack.
template <std::size_t N, std::uint64_t Seed>
class ObfuscatedString {
  std::array<char, N> data_{};

  static constexpr char MaskByte(std::size_t index) noexcept {
    const std::uint64_t mixed = (Seed ^ (index * 0x9e3779b97f4a7c15ULL)) * 0xbf58476d1ce4e5b9ULL;
    return static_cast<char>((mixed >> ((index & 7U) * 8U)) & 0xFFU);
  }

 public:
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  constexpr explicit ObfuscatedString(const char (&str)[N]) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
      data_[i] = static_cast<char>(str[i] ^ MaskByte(i));
    }
  }

  [[nodiscard]] std::string Decrypt() const {
    std::string out(N > 0 ? N - 1 : 0, '\0');
    for (std::size_t i = 0; i + 1 < N; ++i) {
      out[i] = static_cast<char>(data_[i] ^ MaskByte(i));
    }
    return out;
  }
};

}  // namespace octarine::crypto

#define OCT_OBF(str)                                                                                        \
  ([]() -> std::string {                                                                                    \
    constexpr ::octarine::crypto::ObfuscatedString<sizeof(str),                                             \
                                                   0x4f63746172696e65ULL ^ static_cast<uint64_t>(__LINE__)> \
        kObf(str);                                                                                          \
    return kObf.Decrypt();                                                                                  \
  }())
