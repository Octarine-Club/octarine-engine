#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "Crypto/KeyDerivation.h"
#include "Crypto/LuaBytecodeScrambler.h"

// Decrypts/descrambles Lua scripts protected at build/package time.
//
// Scrambled Lua 5.4 binary chunks: starts with \x1bOCT\x54\x00\x19\x93. Restores standard
// \x1bLua\x54 magic, inverts the opcode permutation using the seed derived from the active
// package key, and unmasks string constants in place.
//
// When `requireProtected` is true in strict shipped builds (OCTARINE_SHIPPED &&
// OCTARINE_PROTECT_SCRIPTS && !OCTARINE_ALLOW_LOOSE_ASSETS), unprotected plaintext scripts are
// rejected and cleared.
inline bool DecryptLuaBytes(std::string& bytes, bool requireProtected = false) {
  static constexpr unsigned char kOctMagic[] = {0x1B, 'O', 'C', 'T'};
  constexpr std::size_t kMagicLen = sizeof(kOctMagic);
  if (bytes.size() < kMagicLen || std::memcmp(bytes.data(), kOctMagic, kMagicLen) != 0) {
#if defined(OCTARINE_SHIPPED) && defined(OCTARINE_PROTECT_SCRIPTS) && !defined(OCTARINE_ALLOW_LOOSE_ASSETS)
    if (requireProtected) {
      bytes.clear();
      return false;
    }
#else
    (void)requireProtected;
#endif
    return true;
  }

  // Scrambled Lua 5.4 bytecode chunk (\x1bOCT\x54\x00\x19\x93)
  static constexpr unsigned char kScrambledPrefix[] = {0x1B, 'O', 'C', 'T', 0x54, 0x00, 0x19, 0x93};
  if (bytes.size() >= 34 && std::memcmp(bytes.data(), kScrambledPrefix, sizeof(kScrambledPrefix)) == 0) {
    const uint64_t seed = octarine::crypto::GetActiveBytecodeSeed();
    if (octarine::crypto::DescrambleLua54Bytecode(reinterpret_cast<uint8_t*>(bytes.data()), bytes.size(), seed)) {
      return true;
    }
    bytes.clear();
    return false;
  }

  bytes.clear();
  return false;
}
