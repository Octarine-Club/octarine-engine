#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <lua.hpp>
#include <string_view>

#include "AssetManager/AssetCatalog.h"
#include "AssetManager/AssetPak.h"
#include "Crypto/ChaCha20.h"
#include "Crypto/Hash.h"
#include "Crypto/KeyDerivation.h"
#include "Crypto/LuaBytecodeScrambler.h"
#include "Engine/LuaProtect.h"
#include "TestHarness.h"

using octarine::crypto::ChaCha20;
using octarine::crypto::Fnv1a64;
using octarine::crypto::HashPath;
using octarine::test::Check;
using octarine::test::CheckEq;

int main() {
  std::cout << "[crypto: Fnv1a64 & HashPath]\n";
  {
    constexpr uint64_t emptyHash = Fnv1a64("");
    CheckEq(emptyHash, octarine::crypto::kFnv1aOffsetBasis64, "empty string hash matches offset basis");

    const uint64_t h1 = HashPath("sprites/hero.png");
    const uint64_t h2 = HashPath("./sprites\\Hero.png");
    const uint64_t h3 = HashPath("/sprites/hero.png");
    CheckEq(h1, h2, "path normalization handles backslashes and case");
    CheckEq(h1, h3, "path normalization strips leading slashes");
    Check(h1 != HashPath("sprites/enemy.png"), "different paths produce different hashes");
  }

  std::cout << "[crypto: RFC 8439 Section 2.4.2 test vector]\n";
  {
    constexpr std::array<uint8_t, 32> key = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                             0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                             0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

    constexpr std::array<uint8_t, 12> nonce = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4a, 0x00, 0x00, 0x00, 0x00};

    const std::string_view plaintext =
        "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen "
        "would be it.";

    constexpr std::array<uint8_t, 114> expectedCiphertext = {
        0x6e, 0x2e, 0x35, 0x9a, 0x25, 0x68, 0xf9, 0x80, 0x41, 0xba, 0x07, 0x28, 0xdd, 0x0d, 0x69, 0x81, 0xe9,
        0x7e, 0x7a, 0xec, 0x1d, 0x43, 0x60, 0xc2, 0x0a, 0x27, 0xaf, 0xcc, 0xfd, 0x9f, 0xae, 0x0b, 0xf9, 0x1b,
        0x65, 0xc5, 0x52, 0x47, 0x33, 0xab, 0x8f, 0x59, 0x3d, 0xab, 0xcd, 0x62, 0xb3, 0x57, 0x16, 0x39, 0xd6,
        0x24, 0xe6, 0x51, 0x52, 0xab, 0x8f, 0x53, 0x0c, 0x35, 0x9f, 0x08, 0x61, 0xd8, 0x07, 0xca, 0x0d, 0xbf,
        0x50, 0x0d, 0x6a, 0x61, 0x56, 0xa3, 0x8e, 0x08, 0x8a, 0x22, 0xb6, 0x5e, 0x52, 0xbc, 0x51, 0x4d, 0x16,
        0xcc, 0xf8, 0x06, 0x81, 0x8c, 0xe9, 0x1a, 0xb7, 0x79, 0x37, 0x36, 0x5a, 0xf9, 0x0b, 0xbf, 0x74, 0xa3,
        0x5b, 0xe6, 0xb4, 0x0b, 0x8e, 0xed, 0xf2, 0x78, 0x5e, 0x42, 0x87, 0x4d};

    ChaCha20 cipher(key, nonce, 1);
    std::array<uint8_t, 114> encrypted{};
    cipher.Crypt(reinterpret_cast<const uint8_t*>(plaintext.data()), encrypted.data(), plaintext.size(), 0);

    Check(std::memcmp(encrypted.data(), expectedCiphertext.data(), expectedCiphertext.size()) == 0,
          "ChaCha20 output matches RFC 8439 test vector exactly");

    // Decrypt in place
    cipher.XorInPlace(encrypted.data(), encrypted.size(), 0);
    Check(std::memcmp(encrypted.data(), plaintext.data(), plaintext.size()) == 0,
          "ChaCha20 in-place roundtrip restores original plaintext");
  }

  std::cout << "[crypto: random offset seek consistency]\n";
  {
    constexpr std::array<uint8_t, 32> key = {0x42};
    constexpr std::array<uint8_t, 12> nonce = {0x24};
    ChaCha20 cipher(key, nonce, 0);

    std::vector<uint8_t> buffer(500);
    for (size_t i = 0; i < buffer.size(); ++i) {
      buffer[i] = static_cast<uint8_t>(i & 0xFF);
    }
    std::vector<uint8_t> original = buffer;

    // Encrypt whole buffer
    cipher.XorInPlace(buffer.data(), buffer.size(), 0);

    // Read small slices at arbitrary non-block-aligned offsets
    std::vector<uint8_t> slice1(37);
    cipher.Crypt(buffer.data() + 105, slice1.data(), slice1.size(), 105);
    Check(std::memcmp(slice1.data(), original.data() + 105, slice1.size()) == 0,
          "seeking to offset 105 decrypts matching slice");

    std::vector<uint8_t> slice2(80);
    cipher.Crypt(buffer.data() + 63, slice2.data(), slice2.size(), 63);
    Check(std::memcmp(slice2.data(), original.data() + 63, slice2.size()) == 0,
          "straddling block boundary (offset 63) decrypts matching slice");
  }

  std::cout << "[crypto: Lua 5.4 opcode and bytecode scrambling]\n";
  {
    lua_State* L = luaL_newstate();
    Check(L != nullptr, "luaL_newstate succeeded");

    const char* script =
        "local function add(a, b) return a + b end\n"
        "local msg = 'hello octarine'\n"
        "return add(40, 2)\n";

    int rc = luaL_loadstring(L, script);
    CheckEq(rc, 0, "luaL_loadstring succeeded");

    std::vector<uint8_t> dumped;
    auto dumpWriter = [](lua_State*, const void* p, size_t sz, void* ud) -> int {
      auto* vec = static_cast<std::vector<uint8_t>*>(ud);
      const auto* bytes = static_cast<const uint8_t*>(p);
      vec->insert(vec->end(), bytes, bytes + sz);
      return 0;
    };

    rc = lua_dump(L, dumpWriter, &dumped, 1);  // strip = 1
    CheckEq(rc, 0, "lua_dump stripped bytecode succeeded");
    Check(dumped.size() > 34, "dumped bytecode has valid length");
    Check(dumped[1] == 'L' && dumped[2] == 'u' && dumped[3] == 'a', "initial bytecode has Lua magic");

    std::vector<uint8_t> originalDump = dumped;
    constexpr uint64_t kSeed = 0xDeadBeefCAFE1337ULL;

    // Scramble bytecode
    const bool scrambleOk = octarine::crypto::ScrambleLua54Bytecode(dumped.data(), dumped.size(), kSeed);
    Check(scrambleOk, "ScrambleLua54Bytecode succeeded");
    Check(dumped[1] == 'O' && dumped[2] == 'C' && dumped[3] == 'T', "scrambled magic is OCT");
    Check(dumped != originalDump, "scrambled bytecode is distinct from original");

    // Stock Lua VM rejects the scrambled bytecode
    lua_State* L2 = luaL_newstate();
    int loadScrambledRc = luaL_loadbuffer(L2, reinterpret_cast<const char*>(dumped.data()), dumped.size(), "@test");
    Check(loadScrambledRc != 0, "stock Lua VM rejects scrambled bytecode with non-zero error code");
    lua_close(L2);

    // Descramble bytecode
    const bool descrambleOk = octarine::crypto::DescrambleLua54Bytecode(dumped.data(), dumped.size(), kSeed);
    Check(descrambleOk, "DescrambleLua54Bytecode succeeded");
    Check(dumped[1] == 'L' && dumped[2] == 'u' && dumped[3] == 'a', "descrambled magic is restored to Lua");
    Check(dumped == originalDump, "descrambled bytecode matches original dump byte-for-byte");

    // Execute descrambled bytecode in stock Lua VM
    lua_State* L3 = luaL_newstate();
    int loadDescrambledRc = luaL_loadbuffer(L3, reinterpret_cast<const char*>(dumped.data()), dumped.size(), "@test");
    CheckEq(loadDescrambledRc, 0, "stock Lua VM accepts descrambled bytecode");
    int runRc = lua_pcall(L3, 0, 1, 0);
    CheckEq(runRc, 0, "lua_pcall succeeded on descrambled bytecode");
    CheckEq(static_cast<int>(lua_tointeger(L3, -1)), 42, "script returns 42");

    lua_close(L3);
    lua_close(L);
  }

  std::cout << "[crypto: AssetPak bytecode auto-scrambling & DecryptLuaBytes roundtrip]\n";
  {
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "oct_crypto_test_pak";
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    std::filesystem::create_directories(tempDir, ec);

    const std::filesystem::path scriptFile = tempDir / "bootstrap.lua";
    const std::filesystem::path pakFile = tempDir / "asset_bundle.pak";

    // Generate compiled bytecode for "return 1337"
    lua_State* L = luaL_newstate();
    int rc = luaL_loadstring(L, "local function calc() return 1300 + 37 end return calc()");
    CheckEq(rc, 0, "luaL_loadstring for pak test succeeded");
    std::vector<uint8_t> dumped;
    auto dumpWriter = [](lua_State*, const void* p, size_t sz, void* ud) -> int {
      auto* vec = static_cast<std::vector<uint8_t>*>(ud);
      const auto* bytes = static_cast<const uint8_t*>(p);
      vec->insert(vec->end(), bytes, bytes + sz);
      return 0;
    };
    lua_dump(L, dumpWriter, &dumped, 1);
    lua_close(L);

    // Write original compiled bytecode to disk
    {
      std::ofstream out(scriptFile, std::ios::binary);
      out.write(reinterpret_cast<const char*>(dumped.data()), static_cast<std::streamsize>(dumped.size()));
    }

    // Pack into AssetPak: WriteFileEntry should automatically detect Lua magic and scramble it
    AssetCatalog catalog;
    const bool packOk = AssetPak::Pack(catalog, pakFile.string(), tempDir.string(), {scriptFile.string()}, {});
    Check(packOk, "AssetPak::Pack succeeded with bytecode script");

    // Open pak and read back stream
    AssetPak pak;
    Check(pak.Open(pakFile.string()), "AssetPak::Open succeeded on created pak");
    Check(pak.Contains("bootstrap.lua"), "AssetPak contains bootstrap.lua");

    SDL_IOStream* io = pak.OpenIO("bootstrap.lua");
    Check(io != nullptr, "pak.OpenIO succeeded for bootstrap.lua");
    if (io != nullptr) {
      std::size_t loadedSize = 0;
      void* rawData = SDL_LoadFile_IO(io, &loadedSize, true);
      Check(rawData != nullptr, "SDL_LoadFile_IO succeeded on bootstrap.lua stream");
      if (rawData != nullptr) {
        std::string payload(static_cast<const char*>(rawData), loadedSize);
        SDL_free(rawData);

        // Bytecode stream from pak must have scrambled \x1bOCT magic
        Check(payload.size() >= 34, "payload size is valid");
        Check(payload[0] == '\x1B' && payload[1] == 'O' && payload[2] == 'C' && payload[3] == 'T',
              "bytecode in pak was scrambled with OCT magic");

        // Stock Lua VM fails to load scrambled bytecode
        lua_State* LStock = luaL_newstate();
        int stockRc = luaL_loadbuffer(LStock, payload.data(), payload.size(), "@bootstrap");
        Check(stockRc != 0, "stock Lua VM rejects scrambled bytecode stream");
        lua_close(LStock);

        // DecryptLuaBytes restores original bytecode
        DecryptLuaBytes(payload);
        Check(payload[0] == '\x1B' && payload[1] == 'L' && payload[2] == 'u' && payload[3] == 'a',
              "DecryptLuaBytes restored Lua magic");
        CheckEq(payload.size(), dumped.size(), "descrambled bytecode matches original size");
        Check(std::memcmp(payload.data(), dumped.data(), payload.size()) == 0,
              "descrambled bytecode matches original byte-for-byte");

        // Executing in Lua VM returns 1337
        lua_State* LRun = luaL_newstate();
        int loadRc = luaL_loadbuffer(LRun, payload.data(), payload.size(), "@bootstrap");
        CheckEq(loadRc, 0, "luaL_loadbuffer accepts descrambled bytecode");
        int runRc = lua_pcall(LRun, 0, 1, 0);
        CheckEq(runRc, 0, "lua_pcall succeeded on descrambled bytecode");
        CheckEq(static_cast<int>(lua_tointeger(LRun, -1)), 1337, "descrambled script returns 1337");
        lua_close(LRun);
      }
    }

    // Tamper detection: flipping 1 bit in the TOC or header causes AssetPak::Open to fail cleanly
    {
      std::ifstream pakIn(pakFile, std::ios::binary);
      std::string tampered((std::istreambuf_iterator<char>(pakIn)), std::istreambuf_iterator<char>());
      Check(tampered.size() > 56, "pak file is larger than header");
      // Flip 1 bit in the encrypted TOC at the end of the file
      tampered.back() ^= 0x01;
      const std::filesystem::path tamperedFile = tempDir / "tampered_toc.pak";
      {
        std::ofstream pakOut(tamperedFile, std::ios::binary);
        pakOut.write(tampered.data(), static_cast<std::streamsize>(tampered.size()));
      }
      AssetPak tamperedPak;
      Check(!tamperedPak.Open(tamperedFile.string()), "AssetPak::Open rejects pak with 1-bit tampered TOC");

      // Restore TOC bit, flip 1 bit in header salt (offset 24)
      tampered.back() ^= 0x01;
      tampered[24] ^= 0x01;
      const std::filesystem::path tamperedHeaderFile = tempDir / "tampered_header.pak";
      {
        std::ofstream pakOut(tamperedHeaderFile, std::ios::binary);
        pakOut.write(tampered.data(), static_cast<std::streamsize>(tampered.size()));
      }
      Check(!tamperedPak.Open(tamperedHeaderFile.string()), "AssetPak::Open rejects pak with 1-bit tampered header");
    }

    std::filesystem::remove_all(tempDir, ec);
  }

  std::cout << "[crypto: compile-time string obfuscation (OCT_OBF)]\n";
  {
    const std::string dec = OCT_OBF("Octarine::SecretTestLiteral::2026");
    CheckEq(dec, std::string("Octarine::SecretTestLiteral::2026"), "OCT_OBF decrypts original string at runtime");
  }

  return octarine::test::ReportSummary("CryptoTest");
}
