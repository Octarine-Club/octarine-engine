#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numeric>
#include <vector>

#include "Crypto/Hash.h"

namespace octarine::crypto {

inline constexpr size_t kLua55NumOpcodes = 85;
inline constexpr size_t kLua55MinChunkSize = 42;

// Builds a deterministic 85-opcode bijection from a 64-bit seed.
inline void GenerateOpcodeSBox(uint64_t seed, std::array<uint8_t, kLua55NumOpcodes>& sbox,
                               std::array<uint8_t, kLua55NumOpcodes>& invBox) {
  std::iota(sbox.begin(), sbox.end(), static_cast<uint8_t>(0));
  // Fisher-Yates shuffle with 64-bit LCG
  uint64_t state = seed == 0 ? 0x9e3779b97f4a7c15ULL : seed;
  for (size_t i = kLua55NumOpcodes - 1; i > 0; --i) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    const auto j = static_cast<size_t>(state % (i + 1));
    std::swap(sbox[i], sbox[j]);
  }
  for (size_t i = 0; i < kLua55NumOpcodes; ++i) {
    invBox[sbox[i]] = static_cast<uint8_t>(i);
  }
}

namespace detail {

class BytecodeCursor {
  uint8_t* data_{nullptr};
  size_t size_{0};
  size_t pos_{0};
  uint8_t intSize_{0};
  uint8_t instSize_{0};
  uint8_t luaIntSize_{0};
  uint8_t luaNumSize_{0};
  bool ok_{true};

  uint8_t ReadNumInfoSize() noexcept {
    const uint8_t sz = ReadByte();
    if (!ok_ || sz == 0 || sz > 16) {
      ok_ = false;
      return 0;
    }
    Skip(sz);
    return sz;
  }

  void ProcessConstantEntry(uint8_t tag, uint64_t entrySeed) noexcept {
    switch (tag & 0x3FU) {
      case 0:   // LUA_VNIL
      case 1:   // LUA_VFALSE
      case 17:  // LUA_VTRUE
        break;
      case 19:  // LUA_VNUMFLT
        Skip(luaNumSize_);
        break;
      case 3:  // LUA_VNUMINT (ZigZag MSB varint)
        (void)ReadVarint();
        break;
      case 4:   // LUA_VSHRSTR
      case 20:  // LUA_VLNGSTR
        ProcessString(entrySeed);
        break;
      default:
        ok_ = false;
        break;
    }
  }

 public:
  BytecodeCursor(uint8_t* data, size_t size) : data_(data), size_(size) {}

  [[nodiscard]] bool Ok() const noexcept { return ok_; }
  [[nodiscard]] size_t Pos() const noexcept { return pos_; }

  uint8_t ReadByte() noexcept {
    if (pos_ >= size_) {
      ok_ = false;
      return 0;
    }
    return data_[pos_++];
  }

  void Skip(size_t count) noexcept {
    if (!ok_ || count > size_ - pos_) {
      ok_ = false;
      pos_ = size_;
      return;
    }
    pos_ += count;
  }

  void Align(size_t align) noexcept {
    if (align <= 1 || !ok_) {
      return;
    }
    const size_t rem = pos_ % align;
    if (rem != 0) {
      Skip(align - rem);
    }
  }

  uint64_t ReadVarint() noexcept {
    uint64_t x = 0;
    uint8_t b = 0;
    constexpr uint64_t kLimit = std::numeric_limits<uint64_t>::max() >> 7U;
    do {
      b = ReadByte();
      if (!ok_ || x > kLimit) {
        ok_ = false;
        return 0;
      }
      x = (x << 7U) | static_cast<uint64_t>(b & 0x7FU);
    } while ((b & 0x80U) != 0U);
    return x;
  }

  void SkipHeader(const uint8_t* expectedPrefix) noexcept {
    if (size_ < 12 || std::memcmp(data_, expectedPrefix, 12) != 0) {
      ok_ = false;
      return;
    }
    Skip(12);
    intSize_ = ReadNumInfoSize();
    instSize_ = ReadNumInfoSize();
    luaIntSize_ = ReadNumInfoSize();
    luaNumSize_ = ReadNumInfoSize();
    if (!ok_ || instSize_ != 4 || intSize_ != sizeof(int) || luaNumSize_ != sizeof(double)) {
      ok_ = false;
    }
  }

  void ProcessString(uint64_t strSeed) noexcept {
    const uint64_t rawSize = ReadVarint();
    if (!ok_) return;
    if (rawSize == 0) {
      (void)ReadVarint();  // 0 = NULL, >=1 = reused saved string index
      return;
    }
    if (rawSize > size_ - pos_) {
      ok_ = false;
      return;
    }
    const auto len = static_cast<size_t>(rawSize - 1U);
    for (size_t i = 0; i < len; ++i) {
      data_[pos_ + i] ^= static_cast<uint8_t>((strSeed + i * 31U) & 0xFFU);
    }
    pos_ += len + 1U;  // advance past len content bytes + trailing '\0'
  }

  void ProcessCode(const std::array<uint8_t, kLua55NumOpcodes>& opBox) noexcept {
    const uint64_t codeCount = ReadVarint();
    Align(instSize_);
    if (!ok_ || codeCount > (size_ - pos_) / 4U) {
      ok_ = false;
      return;
    }
    const auto count = static_cast<size_t>(codeCount);
    for (size_t i = 0; i < count; ++i) {
      uint32_t inst = 0;
      std::memcpy(&inst, data_ + pos_, 4);
      const auto op = static_cast<uint8_t>(inst & 0x7FU);
      if (op >= kLua55NumOpcodes) {
        ok_ = false;
        return;
      }
      const uint8_t newOp = opBox[op];
      inst = (inst & ~0x7FU) | static_cast<uint32_t>(newOp);
      std::memcpy(data_ + pos_, &inst, 4);
      pos_ += 4;
    }
  }

  void ProcessConstants(uint64_t seed) noexcept {
    const uint64_t kCount = ReadVarint();
    if (!ok_) return;
    for (uint64_t i = 0; i < kCount && ok_; ++i) {
      const uint8_t tag = ReadByte();
      if (!ok_) return;
      ProcessConstantEntry(tag, seed ^ (i * 17U + 1U));
    }
  }

  void ProcessDebugInfo() noexcept {
    const uint64_t lineinfo = ReadVarint();
    if (!ok_ || lineinfo > size_ - pos_) {
      ok_ = false;
      return;
    }
    Skip(static_cast<size_t>(lineinfo));
    const uint64_t abslineinfo = ReadVarint();
    if (abslineinfo > 0) {
      Align(intSize_);
      const size_t stride = 2U * static_cast<size_t>(intSize_);
      if (!ok_ || abslineinfo > (size_ - pos_) / stride) {
        ok_ = false;
        return;
      }
      Skip(static_cast<size_t>(abslineinfo) * stride);
    }
    const uint64_t locvars = ReadVarint();
    for (uint64_t i = 0; i < locvars && ok_; ++i) {
      ProcessString(0);
      (void)ReadVarint();
      (void)ReadVarint();
    }
    const uint64_t upnames = ReadVarint();
    for (uint64_t i = 0; i < upnames && ok_; ++i) {
      ProcessString(0);
    }
  }

  // Processes function header, code, constants, and upvalues; returns nested proto count.
  size_t ProcessFunctionBody(const std::array<uint8_t, kLua55NumOpcodes>& opBox, uint64_t seed) noexcept {
    if (!ok_ || pos_ >= size_) {
      ok_ = false;
      return 0;
    }
    (void)ReadVarint();  // linedefined
    (void)ReadVarint();  // lastlinedefined
    Skip(3);             // numparams, flag, maxstacksize
    ProcessCode(opBox);
    ProcessConstants(seed);
    const uint64_t upvalCount = ReadVarint();
    if (!ok_ || upvalCount > (size_ - pos_) / 3U) {
      ok_ = false;
      return 0;
    }
    Skip(static_cast<size_t>(upvalCount) * 3U);
    const uint64_t protoCount = ReadVarint();
    if (!ok_ || protoCount > size_ - pos_) {
      ok_ = false;
      return 0;
    }
    return static_cast<size_t>(protoCount);
  }

  void ProcessRootFunction(const std::array<uint8_t, kLua55NumOpcodes>& opBox, uint64_t rootSeed) {
    struct ProtoFrame {
      size_t protoIdx{0};
      size_t protoCount{0};
      uint64_t seed{0};
    };
    std::vector<ProtoFrame> stack;
    const size_t rootProtos = ProcessFunctionBody(opBox, rootSeed);
    if (!ok_) return;
    stack.push_back({0, rootProtos, rootSeed});

    while (!stack.empty() && ok_) {
      auto& top = stack.back();
      if (top.protoIdx < top.protoCount) {
        const uint64_t childSeed = top.seed ^ (static_cast<uint64_t>(top.protoIdx) * 97U + 13U);
        ++top.protoIdx;
        const size_t childProtos = ProcessFunctionBody(opBox, childSeed);
        stack.push_back({0, childProtos, childSeed});
      } else {
        ProcessString(top.seed ^ 0x55U);  // f->source follows dumpProtos in Lua 5.5
        ProcessDebugInfo();
        stack.pop_back();
      }
    }
  }
};

inline bool TransformLua55Bytecode(uint8_t* data, size_t size, uint64_t seed, bool scramble) {
  if (data == nullptr || size < kLua55MinChunkSize) {
    return false;
  }
  static constexpr uint8_t kLuaPrefix[12] = {0x1B, 'L', 'u', 'a', 0x55, 0x00, 0x19, 0x93, '\r', '\n', 0x1A, '\n'};
  static constexpr uint8_t kOctPrefix[12] = {0x1B, 'O', 'C', 'T', 0x55, 0x00, 0x19, 0x93, '\r', '\n', 0x1A, '\n'};

  std::array<uint8_t, kLua55NumOpcodes> sbox{};
  std::array<uint8_t, kLua55NumOpcodes> invBox{};
  GenerateOpcodeSBox(seed, sbox, invBox);

  BytecodeCursor cursor(data, size);
  cursor.SkipHeader(scramble ? kLuaPrefix : kOctPrefix);
  cursor.Skip(1);  // top-level sizeupvalues byte
  cursor.ProcessRootFunction(scramble ? sbox : invBox, seed);

  if (!cursor.Ok() || cursor.Pos() != size) {
    return false;
  }

  data[1] = scramble ? 'O' : 'L';
  data[2] = scramble ? 'C' : 'u';
  data[3] = scramble ? 'T' : 'a';
  return true;
}

}  // namespace detail

// Scrambles a compiled Lua 5.5 binary chunk: swaps magic \x1bLua -> \x1bOCT,
// permutes instructions using a 64-bit seed bijection, and masks string constants.
inline bool ScrambleLua55Bytecode(uint8_t* data, size_t size, uint64_t seed = 0x4f63746172696e65ULL) {
  return detail::TransformLua55Bytecode(data, size, seed, true);
}

// Reverses scrambling on a \x1bOCT chunk, restoring standard Lua 5.5 bytecode.
inline bool DescrambleLua55Bytecode(uint8_t* data, size_t size, uint64_t seed = 0x4f63746172696e65ULL) {
  return detail::TransformLua55Bytecode(data, size, seed, false);
}

}  // namespace octarine::crypto
