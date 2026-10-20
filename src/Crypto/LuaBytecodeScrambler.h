#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <string_view>
#include <vector>

#include "Crypto/Hash.h"

namespace octarine::crypto {

// Builds a deterministic 83-opcode bijection from a 64-bit seed.
inline void GenerateOpcodeSBox(uint64_t seed, std::array<uint8_t, 83>& sbox, std::array<uint8_t, 83>& invBox) {
  std::iota(sbox.begin(), sbox.end(), static_cast<uint8_t>(0));
  // Fisher-Yates shuffle with 64-bit LCG
  uint64_t state = seed == 0 ? 0x9e3779b97f4a7c15ULL : seed;
  for (size_t i = 82; i > 0; --i) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    const auto j = static_cast<size_t>(state % (i + 1));
    std::swap(sbox[i], sbox[j]);
  }
  for (size_t i = 0; i < 83; ++i) {
    invBox[sbox[i]] = static_cast<uint8_t>(i);
  }
}

namespace detail {

class BytecodeCursor {
  uint8_t* data_;
  size_t size_;
  size_t pos_{0};
  bool ok_{true};

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
    if (pos_ + count > size_) {
      ok_ = false;
      pos_ = size_;
      return;
    }
    pos_ += count;
  }

  size_t ReadUnsigned() noexcept {
    size_t x = 0;
    int b = 0;
    do {
      b = ReadByte();
      if (!ok_) return 0;
      x = (x << 7) | static_cast<size_t>(b & 0x7F);
    } while ((b & 0x80) == 0);
    return x;
  }

  void ProcessString(uint64_t strSeed) noexcept {
    const size_t size = ReadUnsigned();
    if (!ok_ || size == 0) return;
    const size_t len = size - 1;
    if (pos_ + len > size_) {
      ok_ = false;
      return;
    }
    for (size_t i = 0; i < len; ++i) {
      data_[pos_ + i] ^= static_cast<uint8_t>((strSeed + i * 31) & 0xFF);
    }
    pos_ += len;
  }

  void ProcessCode(const std::array<uint8_t, 83>& opBox) noexcept {
    const size_t codeCount = ReadUnsigned();
    if (!ok_ || codeCount > (size_ - pos_) / 4) {
      ok_ = false;
      return;
    }
    for (size_t i = 0; i < codeCount; ++i) {
      uint32_t inst = 0;
      std::memcpy(&inst, data_ + pos_, 4);
      const auto op = static_cast<uint8_t>(inst & 0x7F);
      if (op < 83) {
        const uint8_t newOp = opBox[op];
        inst = (inst & ~0x7FU) | static_cast<uint32_t>(newOp);
        std::memcpy(data_ + pos_, &inst, 4);
      }
      pos_ += 4;
    }
  }

  void ProcessConstants(uint64_t seed) noexcept {
    const size_t kCount = ReadUnsigned();
    if (!ok_) return;
    for (size_t i = 0; i < kCount && ok_; ++i) {
      const uint8_t tag = ReadByte();
      switch (tag & 0x3F) {
        case 0:   // LUA_VNIL
        case 1:   // LUA_VFALSE
        case 17:  // LUA_VTRUE
          break;
        case 19:  // LUA_VNUMFLT (8 bytes)
        case 3:   // LUA_VNUMINT (8 bytes)
          Skip(8);
          break;
        case 4:   // LUA_VSHRSTR
        case 20:  // LUA_VLNGSTR
          ProcessString(seed ^ (i * 17 + 1));
          break;
        default:
          ok_ = false;
          return;
      }
    }
  }

  void ProcessDebugInfo() noexcept {
    const size_t lineinfo = ReadUnsigned();
    Skip(lineinfo);
    const size_t abslineinfo = ReadUnsigned();
    for (size_t i = 0; i < abslineinfo && ok_; ++i) {
      ReadUnsigned();
      ReadUnsigned();
    }
    const size_t locvars = ReadUnsigned();
    for (size_t i = 0; i < locvars && ok_; ++i) {
      ProcessString(0);
      ReadUnsigned();
      ReadUnsigned();
    }
    const size_t upnames = ReadUnsigned();
    for (size_t i = 0; i < upnames && ok_; ++i) {
      ProcessString(0);
    }
  }

  // Processes function header, code, constants, and upvalues; returns nested proto count.
  size_t ProcessFunctionBody(const std::array<uint8_t, 83>& opBox, uint64_t seed) noexcept {
    if (!ok_ || pos_ >= size_) {
      ok_ = false;
      return 0;
    }
    ProcessString(seed ^ 0x55);
    ReadUnsigned();  // linedefined
    ReadUnsigned();  // lastlinedefined
    Skip(3);         // numparams, is_vararg, maxstacksize
    ProcessCode(opBox);
    ProcessConstants(seed);
    const size_t upvalCount = ReadUnsigned();
    Skip(upvalCount * 3);
    return ReadUnsigned();  // protoCount
  }

  void ProcessRootFunction(const std::array<uint8_t, 83>& opBox, uint64_t rootSeed) {
    struct ProtoFrame {
      size_t protoIdx;
      size_t protoCount;
      uint64_t seed;
    };
    std::vector<ProtoFrame> stack;
    const size_t rootProtos = ProcessFunctionBody(opBox, rootSeed);
    if (!ok_) return;
    stack.push_back({0, rootProtos, rootSeed});

    while (!stack.empty() && ok_) {
      auto& top = stack.back();
      if (top.protoIdx < top.protoCount) {
        const uint64_t childSeed = top.seed ^ (top.protoIdx * 97 + 13);
        ++top.protoIdx;
        const size_t childProtos = ProcessFunctionBody(opBox, childSeed);
        stack.push_back({0, childProtos, childSeed});
      } else {
        ProcessDebugInfo();
        stack.pop_back();
      }
    }
  }
};

}  // namespace detail

// Scrambles a compiled Lua 5.4 binary chunk: swaps magic \x1bLua -> \x1bOCT,
// permutes instructions using a 64-bit seed bijection, and masks string constants.
inline bool ScrambleLua54Bytecode(uint8_t* data, size_t size, uint64_t seed = 0x4f63746172696e65ULL) {
  if (data == nullptr || size < 34) return false;
  static constexpr uint8_t kLuaMagic[] = {0x1B, 'L', 'u', 'a', 0x54};
  if (std::memcmp(data, kLuaMagic, 5) != 0) {
    return false;
  }

  std::array<uint8_t, 83> sbox{};
  std::array<uint8_t, 83> invBox{};
  GenerateOpcodeSBox(seed, sbox, invBox);

  detail::BytecodeCursor cursor(data, size);
  cursor.Skip(31);  // 31-byte Lua 5.4 header
  cursor.Skip(1);   // top-level sizeupvalues byte
  cursor.ProcessRootFunction(sbox, seed);

  if (!cursor.Ok()) {
    return false;
  }

  // Mutate header magic to \x1bOCT
  data[1] = 'O';
  data[2] = 'C';
  data[3] = 'T';
  return true;
}

// Reverses scrambling on a \x1bOCT chunk, restoring standard Lua 5.4 bytecode.
inline bool DescrambleLua54Bytecode(uint8_t* data, size_t size, uint64_t seed = 0x4f63746172696e65ULL) {
  if (data == nullptr || size < 34) return false;
  static constexpr uint8_t kOctMagic[] = {0x1B, 'O', 'C', 'T', 0x54};
  if (std::memcmp(data, kOctMagic, 5) != 0) {
    return false;
  }

  std::array<uint8_t, 83> sbox{};
  std::array<uint8_t, 83> invBox{};
  GenerateOpcodeSBox(seed, sbox, invBox);

  detail::BytecodeCursor cursor(data, size);
  cursor.Skip(31);  // 31-byte Lua 5.4 header
  cursor.Skip(1);
  cursor.ProcessRootFunction(invBox, seed);

  if (!cursor.Ok()) {
    return false;
  }

  // Restore header magic to \x1bLua
  data[1] = 'L';
  data[2] = 'u';
  data[3] = 'a';
  return true;
}

}  // namespace octarine::crypto
