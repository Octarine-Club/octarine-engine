#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

#include "Crypto/ChaCha20.h"
#include "Crypto/Hash.h"
#include "Crypto/ObfuscatedString.h"

namespace octarine::crypto {

// Expands a string or byte seed into a 32-byte (256-bit) encryption key.
inline std::array<uint8_t, 32> DeriveKey256(std::string_view seed) {
  std::array<uint8_t, 32> key{};
  std::string fallback;
  if (seed.empty()) {
    fallback = OCT_OBF("OctarineEngine::DefaultPackageSecretSalt");
    seed = fallback;
  }

  // Multi-pass deterministic expansion
  uint64_t h1 = Fnv1a64(seed, 0xcbf29ce484222325ULL);
  uint64_t h2 = Fnv1a64(seed, 0x811c9dc5ULL);
  uint64_t h3 = Fnv1a64(seed, h1 ^ 0xa5a5a5a5a5a5a5a5ULL);
  uint64_t h4 = Fnv1a64(seed, h2 ^ 0x5a5a5a5a5a5a5a5aULL);

  std::memcpy(key.data() + 0, &h1, 8);
  std::memcpy(key.data() + 8, &h2, 8);
  std::memcpy(key.data() + 16, &h3, 8);
  std::memcpy(key.data() + 24, &h4, 8);

  // Self-mixing round with fixed salt
  constexpr std::array<uint8_t, 12> kMixNonce = {0x4f, 0x63, 0x74, 0x61, 0x72, 0x69,
                                                 0x6e, 0x65, 0x50, 0x61, 0x6b, 0x32};
  ChaCha20 mixer(key, kMixNonce, 0);
  mixer.XorInPlace(key.data(), key.size(), 0);
  return key;
}

// Computes a 128-bit (16-byte) keyed MAC over `data` using `key` and `salt`.
inline std::array<uint8_t, 16> ComputeKeyedMac128(std::span<const uint8_t, 32> key, std::span<const uint8_t, 16> salt,
                                                  const void* data, size_t size) noexcept {
  uint64_t k0 = 0;
  uint64_t k1 = 0;
  std::memcpy(&k0, key.data(), 8);
  std::memcpy(&k1, key.data() + 8, 8);

  uint64_t s0 = 0;
  uint64_t s1 = 0;
  std::memcpy(&s0, salt.data(), 8);
  std::memcpy(&s1, salt.data() + 8, 8);

  const uint64_t lane0 = Fnv1a64(data, size, kFnv1aOffsetBasis64 ^ k0 ^ s0);
  const uint64_t lane1 = Fnv1a64(data, size, (kFnv1aOffsetBasis64 ^ k1 ^ s1) + static_cast<uint64_t>(size));

  std::array<uint8_t, 16> mac{};
  std::memcpy(mac.data(), &lane0, 8);
  std::memcpy(mac.data() + 8, &lane1, 8);

  std::array<uint8_t, 12> macNonce{};
  std::memcpy(macNonce.data(), &lane0, 8);
  const uint32_t fold = static_cast<uint32_t>(lane1) ^ static_cast<uint32_t>(lane1 >> 32U);
  std::memcpy(macNonce.data() + 8, &fold, 4);

  ChaCha20 prf(key, macNonce, 0x7FFFFFFFU);
  prf.XorInPlace(mac.data(), mac.size(), 0);
  return mac;
}

// Returns the active package encryption key.
inline std::array<uint8_t, 32> GetActivePackageKey() {
#if defined(OCTARINE_PACKAGE_KEY)
  const std::string customKey = OCT_OBF(OCTARINE_PACKAGE_KEY);
  return DeriveKey256(customKey);
#else
  return DeriveKey256("");
#endif
}

// Returns a 64-bit seed derived from the active package encryption key for bytecode scrambling.
inline uint64_t GetActiveBytecodeSeed() {
  const auto key = GetActivePackageKey();
  uint64_t seed = 0;
  std::memcpy(&seed, key.data(), sizeof(seed));
  return seed;
}

}  // namespace octarine::crypto
