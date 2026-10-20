#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace octarine::crypto {

// RFC 8439 ChaCha20 stream cipher with arbitrary byte offset seeking.
class ChaCha20 {
 public:
  static constexpr size_t kKeySize = 32;
  static constexpr size_t kNonceSize = 12;
  static constexpr size_t kBlockSize = 64;

 private:
  std::array<uint32_t, 8> key_{};
  std::array<uint32_t, 3> nonce_{};
  uint32_t initial_counter_{0};

  static inline void QuarterRound(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) noexcept {
    a += b;
    d ^= a;
    d = std::rotl(d, 16);
    c += d;
    b ^= c;
    b = std::rotl(b, 12);
    a += b;
    d ^= a;
    d = std::rotl(d, 8);
    c += d;
    b ^= c;
    b = std::rotl(b, 7);
  }

  static void InnerBlock(std::array<uint32_t, 16>& x) noexcept {
    // 10 double-rounds = 20 rounds
    for (int r = 0; r < 10; ++r) {
      QuarterRound(x[0], x[4], x[8], x[12]);
      QuarterRound(x[1], x[5], x[9], x[13]);
      QuarterRound(x[2], x[6], x[10], x[14]);
      QuarterRound(x[3], x[7], x[11], x[15]);

      QuarterRound(x[0], x[5], x[10], x[15]);
      QuarterRound(x[1], x[6], x[11], x[12]);
      QuarterRound(x[2], x[7], x[8], x[13]);
      QuarterRound(x[3], x[4], x[9], x[14]);
    }
  }

  void GenerateBlock(uint32_t blockCounter, std::array<uint8_t, kBlockSize>& out) const noexcept {
    // "expand 32-byte k" constants
    std::array<uint32_t, 16> state{0x61707865,   0x3320646e, 0x79622d32, 0x6b206574, key_[0], key_[1],
                                   key_[2],      key_[3],    key_[4],    key_[5],    key_[6], key_[7],
                                   blockCounter, nonce_[0],  nonce_[1],  nonce_[2]};

    std::array<uint32_t, 16> workingState = state;
    InnerBlock(workingState);

    for (size_t i = 0; i < 16; ++i) {
      const uint32_t word = workingState[i] + state[i];
      // Serialized little-endian
      out[i * 4 + 0] = static_cast<uint8_t>(word & 0xFF);
      out[i * 4 + 1] = static_cast<uint8_t>((word >> 8) & 0xFF);
      out[i * 4 + 2] = static_cast<uint8_t>((word >> 16) & 0xFF);
      out[i * 4 + 3] = static_cast<uint8_t>((word >> 24) & 0xFF);
    }
  }

 public:
  ChaCha20() = default;

  ChaCha20(std::span<const uint8_t, kKeySize> key, std::span<const uint8_t, kNonceSize> nonce,
           uint32_t initialCounter = 0) noexcept {
    SetKey(key);
    SetNonce(nonce, initialCounter);
  }

  void SetKey(std::span<const uint8_t, kKeySize> key) noexcept {
    for (size_t i = 0; i < 8; ++i) {
      key_[i] = static_cast<uint32_t>(key[i * 4 + 0]) | (static_cast<uint32_t>(key[i * 4 + 1]) << 8) |
                (static_cast<uint32_t>(key[i * 4 + 2]) << 16) | (static_cast<uint32_t>(key[i * 4 + 3]) << 24);
    }
  }

  void SetNonce(std::span<const uint8_t, kNonceSize> nonce, uint32_t initialCounter = 0) noexcept {
    initial_counter_ = initialCounter;
    for (size_t i = 0; i < 3; ++i) {
      nonce_[i] = static_cast<uint32_t>(nonce[i * 4 + 0]) | (static_cast<uint32_t>(nonce[i * 4 + 1]) << 8) |
                  (static_cast<uint32_t>(nonce[i * 4 + 2]) << 16) | (static_cast<uint32_t>(nonce[i * 4 + 3]) << 24);
    }
  }

  // Encrypts or decrypts bytes in-place starting at stream `byteOffset`.
  void XorInPlace(uint8_t* data, size_t length, uint64_t byteOffset = 0) const noexcept {
    Crypt(data, data, length, byteOffset);
  }

  // Encrypts or decrypts `in` into `out` starting at stream `byteOffset`.
  void Crypt(const uint8_t* in, uint8_t* out, size_t length, uint64_t byteOffset = 0) const noexcept {
    if (length == 0 || in == nullptr || out == nullptr) {
      return;
    }

    uint64_t blockIndex = byteOffset / kBlockSize;
    auto blockOffset = static_cast<size_t>(byteOffset % kBlockSize);
    std::array<uint8_t, kBlockSize> keyStream{};

    size_t bytesProcessed = 0;
    while (bytesProcessed < length) {
      const uint32_t counter = initial_counter_ + static_cast<uint32_t>(blockIndex);
      GenerateBlock(counter, keyStream);

      const size_t take = std::min(length - bytesProcessed, kBlockSize - blockOffset);
      for (size_t i = 0; i < take; ++i) {
        out[bytesProcessed + i] = in[bytesProcessed + i] ^ keyStream[blockOffset + i];
      }

      bytesProcessed += take;
      blockOffset = 0;
      ++blockIndex;
    }
  }
};

}  // namespace octarine::crypto
