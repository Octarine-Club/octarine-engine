// Runtime smoke test for baked AssetPak (.pak) loading under strict shipped/no-loose-assets
// semantics (ENG-DX-06):
//   1. Baked .pak loads config, manifest, textures, fonts, and scrambled Lua 5.5 bytecode with
//      zero loose source files on disk.
//   2. Loose-file overrides are ignored for packed entries, loose unpacked files are rejected
//      when allowLooseAssets == false, and unscrambled plaintext scripts / loose manifests fail.
//   3. Opening an OCPK v1 archive or scrambled Lua chunk encrypted with a mismatched package key
//      fails cleanly without crashing or exposing partial TOC entries.

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <lua.hpp>
#include <sol/sol.hpp>
#include <string>
#include <vector>

#include "AssetManager/AssetCatalog.h"
#include "AssetManager/AssetManager.h"
#include "AssetManager/AssetPak.h"
#include "Crypto/ChaCha20.h"
#include "Crypto/Hash.h"
#include "Crypto/KeyDerivation.h"
#include "Crypto/LuaBytecodeScrambler.h"
#include "Crypto/LuaProtect.h"
#include "Game/GameConfig.h"
#include "General/Logger.h"
#include "TestHarness.h"

using octarine::test::Check;
using octarine::test::CheckEq;

namespace {

constexpr unsigned char kMinimalTestTtf[] = {
    0x00, 0x01, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x80, 0x00, 0x03, 0x00, 0x20, 0x4F, 0x53, 0x2F, 0x32, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xAC, 0x00, 0x00, 0x00, 0x56, 0x63, 0x6D, 0x61, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x04, 0x00, 0x00, 0x00, 0x2C, 0x67, 0x6C, 0x79, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x30, 0x00,
    0x00, 0x00, 0x22, 0x68, 0x65, 0x61, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x54, 0x00, 0x00, 0x00, 0x36,
    0x68, 0x68, 0x65, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x8C, 0x00, 0x00, 0x00, 0x24, 0x68, 0x6D, 0x74,
    0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xB0, 0x00, 0x00, 0x00, 0x06, 0x6C, 0x6F, 0x63, 0x61, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0xB8, 0x00, 0x00, 0x00, 0x06, 0x6D, 0x61, 0x78, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0xC0, 0x00, 0x00, 0x00, 0x20, 0x6E, 0x61, 0x6D, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xE0,
    0x00, 0x00, 0x00, 0x06, 0x70, 0x6F, 0x73, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xE8, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x01, 0x02, 0x58, 0x01, 0x90, 0x00, 0x05, 0x00, 0x00, 0x01, 0xF4, 0x01, 0xF4, 0x00, 0x00, 0x00, 0x64,
    0x01, 0xF4, 0x01, 0xF4, 0x00, 0x00, 0x01, 0x2C, 0x00, 0x32, 0x00, 0xFA, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x54, 0x45, 0x53, 0x54, 0x00, 0x40, 0x00, 0x20, 0x00, 0x7E, 0x03, 0x20, 0xFF, 0x38, 0x00, 0x00, 0x03,
    0x20, 0x00, 0xC8, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x04, 0x00, 0x20, 0x00, 0x00, 0x00, 0x04, 0x00, 0x04, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x7E, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x20, 0xFF, 0xFF, 0xFF, 0xE1, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x32, 0x00, 0x00, 0x02, 0x26, 0x02, 0xBC, 0x00, 0x03, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00,
    0x32, 0x01, 0xF4, 0x00, 0x00, 0xFE, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x02, 0xBC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5F, 0x0F, 0x3C, 0xF5, 0x00, 0x0B, 0x03, 0xE8, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00,
    0x02, 0x26, 0x02, 0xBC, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x03, 0x20, 0xFF, 0x38, 0x00, 0x00, 0x02, 0x58, 0x00, 0x32, 0x00, 0x00, 0x02, 0x26, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x58, 0x00, 0x32, 0x00,
    0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xCE, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

#pragma pack(push, 1)
struct SyntheticPakHeader {
  char magic[4];
  std::uint32_t version;
  std::uint64_t tocOffset;
  std::uint32_t tocCount;
  std::uint32_t flags;
  std::uint8_t salt[16];
  std::uint8_t tocMac[16];
};

struct SyntheticPakTocEntry {
  std::uint64_t pathHash;
  std::uint64_t offset;
  std::uint64_t size;
};
#pragma pack(pop)

void WriteTextFile(const std::filesystem::path& path, const std::string& content) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

bool WriteSolidPng(const std::filesystem::path& path, const int w, const int h, const Uint8 r, const Uint8 g,
                   const Uint8 b) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  SDL_Surface* surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
  if (surf == nullptr) {
    return false;
  }
  const Uint32 pixel = SDL_MapSurfaceRGBA(surf, r, g, b, 255);
  SDL_FillSurfaceRect(surf, nullptr, pixel);
  const bool ok = IMG_SavePNG(surf, path.string().c_str());
  SDL_DestroySurface(surf);
  return ok;
}

std::vector<std::uint8_t> CompileLuaBytecode(const std::string& source, const char* chunkName) {
  lua_State* L = luaL_newstate();
  std::vector<std::uint8_t> dumped;
  if (L == nullptr) {
    return dumped;
  }
  if (luaL_loadbuffer(L, source.data(), source.size(), chunkName) == 0) {
    auto writer = [](lua_State*, const void* p, const std::size_t sz, void* ud) -> int {
      auto* vec = static_cast<std::vector<std::uint8_t>*>(ud);
      const auto* bytes = static_cast<const std::uint8_t*>(p);
      vec->insert(vec->end(), bytes, bytes + sz);
      return 0;
    };
    lua_dump(L, writer, &dumped, 1);
  }
  lua_close(L);
  return dumped;
}

std::string ReadStreamToString(SDL_IOStream* io) {
  if (io == nullptr) {
    return {};
  }
  std::size_t size = 0;
  void* raw = SDL_LoadFile_IO(io, &size, true);
  if (raw == nullptr) {
    return {};
  }
  std::string out(static_cast<const char*>(raw), size);
  SDL_free(raw);
  return out;
}

bool BuildBakedPakFixture(const std::filesystem::path& sourceDir, const std::filesystem::path& outPakPath) {
  WriteTextFile(sourceDir / "config.ini",
                "Title=ShippedSmoke\nStartupScript=main.lua\nBaseResolutionWidth=320\nBaseResolutionHeight=240\n");
  WriteTextFile(sourceDir / "main.lua", "return 'pak_main_v1'\n");
  if (!WriteSolidPng(sourceDir / "sprites" / "hero.png", 4, 4, 40, 180, 255)) {
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(sourceDir / "fonts", ec);
  {
    std::ofstream fontOut(sourceDir / "fonts" / "ui.ttf", std::ios::binary | std::ios::trunc);
    fontOut.write(reinterpret_cast<const char*>(kMinimalTestTtf), sizeof(kMinimalTestTtf));
  }

  const auto bytecode = CompileLuaBytecode("return 777", "=@scripts/protected.lua");
  if (bytecode.empty()) {
    return false;
  }
  std::filesystem::create_directories(sourceDir / "scripts", ec);
  {
    std::ofstream scriptOut(sourceDir / "scripts" / "protected.lua", std::ios::binary | std::ios::trunc);
    scriptOut.write(reinterpret_cast<const char*>(bytecode.data()), static_cast<std::streamsize>(bytecode.size()));
  }

  sol::state scanLua;
  scanLua.open_libraries(sol::lib::base, sol::lib::table);
  AssetCatalog catalog;
  if (!catalog.ScanFilesystem(sourceDir.string(), scanLua, ScaleMode::Nearest)) {
    return false;
  }
  const std::filesystem::path manifestPath = sourceDir / AssetCatalog::kManifestFilename;
  if (!catalog.WriteManifest(manifestPath.string(), sourceDir.string())) {
    return false;
  }

  const std::vector<std::string> extraFiles = {
      (sourceDir / "config.ini").string(),
      (sourceDir / "main.lua").string(),
      (sourceDir / "scripts" / "protected.lua").string(),
      manifestPath.string(),
  };
  return AssetPak::Pack(catalog, outPakPath.string(), sourceDir.string(), extraFiles);
}

void TestShippedPakLoadAndLooseAssetRejection(SDL_Renderer* sdlRenderer) {
  std::cout << "[shipped-pak: zero-loose-files boot + loose-override/unpacked rejection]\n";

  const std::filesystem::path rootTmp = std::filesystem::temp_directory_path() / "octarine_shipped_pak_smoke";
  const std::filesystem::path srcDir = rootTmp / "authoring_src";
  const std::filesystem::path runDir = rootTmp / "shipped_runtime";
  std::error_code ec;
  std::filesystem::remove_all(rootTmp, ec);
  std::filesystem::create_directories(srcDir, ec);
  std::filesystem::create_directories(runDir, ec);

  const std::filesystem::path pakPath = runDir / AssetPak::kDefaultFilename;
  Check(BuildBakedPakFixture(srcDir, pakPath), "baked OCPK v1 fixture archive written");

  // Delete the entire authoring tree so zero loose files exist anywhere on disk.
  std::filesystem::remove_all(srcDir, ec);

  AssetPak pak;
  Check(pak.Open(pakPath.string()), "AssetPak::Open succeeds in shipped runtime dir with zero loose files");
  Check(pak.IsOpen() && pak.IsEncrypted() && pak.Version() == 1U, "pak reports open encrypted OCPK v1");

  GameConfig config;
  SDL_IOStream* cfgIo = pak.OpenIO(GameConfig::kConfigFileName);
  Check(cfgIo != nullptr && config.LoadConfigFromStream(cfgIo, runDir.string()),
        "GameConfig loads from AssetPak stream without loose config.ini");

  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::table);

  AssetManager am;
  am.LoadGameConfig(config);
  am.SetAssetPak(&pak);
  Check(am.GetCatalog().Build(runDir.string(), lua, config, /*allowManifest=*/true, &pak),
        "AssetCatalog::Build loads embedded asset_manifest.lua from AssetPak");
  Check(am.GetCatalog().IsFromManifest(), "shipped catalog reports manifest origin");
  Check(am.Acquire("hero", sdlRenderer, nullptr) && am.GetTexture("hero") != nullptr,
        "Acquire('hero') loads encrypted PNG from AssetPak");
  Check(am.Acquire("ui-16", sdlRenderer, nullptr) && am.GetFont("ui-16") != nullptr,
        "Acquire('ui-16') loads encrypted TTF font from AssetPak");

  std::string protectedChunk = ReadStreamToString(am.OpenAssetIO(am.GetFullPath("scripts/protected.lua")));
  Check(!protectedChunk.empty() && DecryptLuaBytes(protectedChunk, /*requireProtected=*/true),
        "DecryptLuaBytes descrambles protected Lua 5.5 chunk from AssetPak");
  const auto runRes = lua.safe_script(protectedChunk, sol::script_pass_on_error, "@scripts/protected.lua");
  Check(runRes.valid() && runRes.get<int>() == 777, "descrambled Lua 5.5 bytecode executes and returns 777");

  // Loose override attempt: plant tampered files beside asset_bundle.pak and verify shipped mode ignores/rejects them.
  WriteTextFile(runDir / "scripts" / "protected.lua", "return -999\n");
  WriteTextFile(runDir / "sprites" / "hero.png", "CORRUPT_LOOSE_PNG_OVERRIDE");
  WriteTextFile(runDir / "loose_only_injection.lua", "return 'hijack'\n");
  WriteSolidPng(runDir / "sprites" / "loose_extra.png", 4, 4, 255, 0, 0);

  std::string afterOverride = ReadStreamToString(am.OpenAssetIO(am.GetFullPath("scripts/protected.lua")));
  Check(DecryptLuaBytes(afterOverride, /*requireProtected=*/true), "override probe still serves scrambled pak script");
  const auto overrideRes = lua.safe_script(afterOverride, sol::script_pass_on_error, "@scripts/protected.lua");
  Check(overrideRes.valid() && overrideRes.get<int>() == 777,
        "loose scripts/protected.lua override does not shadow AssetPak entry");
  Check(am.Reload("hero", sdlRenderer, nullptr) && am.GetTexture("hero") != nullptr,
        "Reload('hero') reads valid PNG from AssetPak instead of corrupt loose override");

  Check(am.OpenAssetIO(am.GetFullPath("loose_only_injection.lua")) == nullptr,
        "OpenAssetIO rejects loose file absent from AssetPak when allowLooseAssets == false");
  am.AddTexture(sdlRenderer, "loose_extra", "sprites/loose_extra.png");
  Check(am.GetTexture("loose_extra") == nullptr,
        "AddTexture rejects loose PNG absent from AssetPak when allowLooseAssets == false");

  std::string rawPlaintext = "return 123\n";
  Check(!DecryptLuaBytes(rawPlaintext, /*requireProtected=*/true) && rawPlaintext.empty(),
        "DecryptLuaBytes rejects unscrambled plaintext script when requireProtected is true");

  const std::filesystem::path looseManifestDir = rootTmp / "loose_manifest_no_pak";
  WriteTextFile(looseManifestDir / AssetCatalog::kManifestFilename, "return {}\n");
  AssetCatalog rejectedCatalog;
  Check(!rejectedCatalog.Build(looseManifestDir.string(), lua, std::nullopt, /*allowManifest=*/true, /*pak=*/nullptr),
        "AssetCatalog::Build rejects loose asset_manifest.lua when no AssetPak is open");

  am.ClearAssets();
  std::filesystem::remove_all(rootTmp, ec);
}

bool WriteWrongKeyOcpk(const std::filesystem::path& pakPath, const std::array<std::uint8_t, 32>& wrongKey) {
  const std::string payloadPlain = "return 'wrong_key_payload'\n";
  const std::uint64_t pathHash = octarine::crypto::HashPath("main.lua");
  const std::array<std::uint8_t, 16> salt = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

  std::vector<std::uint8_t> encPayload(payloadPlain.begin(), payloadPlain.end());
  std::array<std::uint8_t, 12> entryNonce{};
  std::memcpy(entryNonce.data(), &pathHash, 8);
  std::memcpy(entryNonce.data() + 8, salt.data() + 12, 4);
  octarine::crypto::ChaCha20 payloadCipher(wrongKey, entryNonce, 0);
  payloadCipher.XorInPlace(encPayload.data(), encPayload.size(), 0);

  SyntheticPakTocEntry plainToc{pathHash, sizeof(SyntheticPakHeader), encPayload.size()};
  SyntheticPakHeader hdr{};
  std::memcpy(hdr.magic, "OCPK", 4);
  hdr.version = 1;
  hdr.tocOffset = sizeof(SyntheticPakHeader) + encPayload.size();
  hdr.tocCount = 1;
  hdr.flags = 1;
  std::memcpy(hdr.salt, salt.data(), salt.size());

  std::vector<std::uint8_t> macInput(24 + sizeof(SyntheticPakTocEntry));
  std::memcpy(macInput.data(), &hdr, 24);
  std::memcpy(macInput.data() + 24, &plainToc, sizeof(plainToc));
  const auto mac = octarine::crypto::ComputeKeyedMac128(wrongKey, salt, macInput.data(), macInput.size());
  std::memcpy(hdr.tocMac, mac.data(), mac.size());

  SyntheticPakTocEntry encToc = plainToc;
  std::array<std::uint8_t, 12> tocNonce{};
  std::memcpy(tocNonce.data(), salt.data(), 12);
  octarine::crypto::ChaCha20 tocCipher(wrongKey, tocNonce, 0);
  tocCipher.XorInPlace(reinterpret_cast<std::uint8_t*>(&encToc), sizeof(encToc), 0);

  std::ofstream out(pakPath, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  out.write(reinterpret_cast<const char*>(encPayload.data()), static_cast<std::streamsize>(encPayload.size()));
  out.write(reinterpret_cast<const char*>(&encToc), sizeof(encToc));
  return static_cast<bool>(out);
}

void TestWrongPackageKeyRejection() {
  std::cout << "[shipped-pak: wrong package key fails cleanly]\n";

  const std::filesystem::path tmpDir = std::filesystem::temp_directory_path() / "octarine_shipped_wrong_key";
  std::error_code ec;
  std::filesystem::remove_all(tmpDir, ec);
  std::filesystem::create_directories(tmpDir, ec);

  const auto wrongKey = octarine::crypto::DeriveKey256("OctarineEngine::MismatchedPackageSecret::SmokeTest");
  Check(wrongKey != octarine::crypto::GetActivePackageKey(), "synthetic wrong key differs from active package key");

  const std::filesystem::path wrongPakPath = tmpDir / "wrong_key.pak";
  Check(WriteWrongKeyOcpk(wrongPakPath, wrongKey), "wrote structurally valid OCPK v1 with mismatched key");

  AssetPak pak;
  Check(!pak.Open(wrongPakPath.string()), "AssetPak::Open rejects valid OCPK v1 encrypted with wrong package key");
  Check(!pak.IsOpen(), "failed wrong-key Open leaves IsOpen() false");
  CheckEq(pak.Size(), std::size_t{0}, "failed wrong-key Open exposes zero TOC entries");
  Check(pak.OpenIO("main.lua") == nullptr, "OpenIO on rejected wrong-key pak returns nullptr");

  auto bytecode = CompileLuaBytecode("return 42", "=@wrong_seed.lua");
  std::uint64_t wrongSeed = 0;
  std::memcpy(&wrongSeed, wrongKey.data(), sizeof(wrongSeed));
  Check(octarine::crypto::ScrambleLua55Bytecode(bytecode.data(), bytecode.size(), wrongSeed),
        "compiled Lua 5.5 chunk scrambled with mismatched key seed");

  std::string scrambledStr(reinterpret_cast<const char*>(bytecode.data()), bytecode.size());
  // Even if DescrambleLua55Bytecode inverts structurally, loading the wrong-seed chunk in Lua must never execute as 42.
  const bool descrambled = DecryptLuaBytes(scrambledStr, /*requireProtected=*/true);
  bool wrongChunkRanCleanly = false;
  if (descrambled && !scrambledStr.empty()) {
    lua_State* L = luaL_newstate();
    if (L != nullptr) {
      if (luaL_loadbuffer(L, scrambledStr.data(), scrambledStr.size(), "@wrong_seed.lua") == 0 &&
          lua_pcall(L, 0, 1, 0) == 0 && lua_isinteger(L, -1) && lua_tointeger(L, -1) == 42) {
        wrongChunkRanCleanly = true;
      }
      lua_close(L);
    }
  }
  Check(!wrongChunkRanCleanly, "Lua 5.5 bytecode scrambled with wrong package key cannot decode and return 42");

  std::filesystem::remove_all(tmpDir, ec);
}

}  // namespace

int main() {
  Logger::Init();
#if !defined(OCTARINE_SHIPPED) || defined(OCTARINE_ALLOW_LOOSE_ASSETS)
  Check(false,
        "OctarineShippedPakSmokeTest must compile with OCTARINE_SHIPPED and without OCTARINE_ALLOW_LOOSE_ASSETS");
#endif

  SDL_Init(0);
  TTF_Init();
  SDL_Surface* surf = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32);
  SDL_Renderer* sdlRenderer = surf != nullptr ? SDL_CreateSoftwareRenderer(surf) : nullptr;
  Check(sdlRenderer != nullptr, "headless software SDL_Renderer created");

  if (sdlRenderer != nullptr) {
    TestShippedPakLoadAndLooseAssetRejection(sdlRenderer);
    TestWrongPackageKeyRejection();
    SDL_DestroyRenderer(sdlRenderer);
  }
  if (surf != nullptr) {
    SDL_DestroySurface(surf);
  }
  TTF_Quit();
  SDL_Quit();
  return octarine::test::ReportSummary("OctarineShippedPakSmokeTest");
}
