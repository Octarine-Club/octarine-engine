#pragma once

#include <SDL3/SDL_iostream.h>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class AssetCatalog;

// Packed archive of baked assets and scripts, opened as a single blob at startup.
//
// Format ("OCPK", little-endian):
//   [56] PakHeader:
//          char magic[4] = "OCPK"
//          u32  version = 1
//          u64  tocOffset
//          u32  tocCount
//          u32  flags (bit 0: ChaCha20 encrypted)
//          u8   salt[16]
//          u8   tocMac[16] (128-bit keyed MAC over header metadata + TOC)
//   [data...]   ChaCha20-encrypted asset payloads
//   [TOC...]    Encrypted array of PakTocEntry:
//                 u64 pathHash (64-bit FNV-1a of normalized project-relative path)
//                 u64 offset (from file start)
//                 u64 size
//
// Filenames and directory hierarchies are never stored in the archive. Data is decrypted
// on-the-fly at 64-byte block granularity via an SDL_IOStreamInterface backed by ChaCha20, so
// `data_` remains completely encrypted in RAM.
class AssetPak {
 public:
  static constexpr const char* kDefaultFilename = "asset_bundle.pak";

  AssetPak() = default;
  AssetPak(const AssetPak&) = delete;
  AssetPak& operator=(const AssetPak&) = delete;
  AssetPak(AssetPak&&) noexcept;
  AssetPak& operator=(AssetPak&&) noexcept;
  ~AssetPak();

  // Pack every entry in `catalog` (+ extraFiles like scripts and manifests) into a hardened
  // OCPK archive at `outPath`.
  static bool Pack(const AssetCatalog& catalog, const std::string& outPath, const std::string& basePath,
                   const std::vector<std::string>& extraFiles = {},
                   const std::map<std::string, std::string>& pathOverrides = {});

  // Open a pak file from disk into memory. Returns true on success.
  bool Open(const std::string& pakPath);

  // True when Open() succeeded.
  [[nodiscard]] bool IsOpen() const { return data_ != nullptr; }

  // True when `relPath` (project-relative, forward slashes) is a stored entry.
  [[nodiscard]] bool Contains(const std::string& relPath) const;

  // Open a read-only seekable ChaCha20 SDL_IOStream over the pak entry at `relPath` that decrypts
  // on-the-fly. Returns nullptr if absent.
  [[nodiscard]] SDL_IOStream* OpenIO(const std::string& relPath) const;

  [[nodiscard]] std::size_t Size() const { return entries_.size(); }
  [[nodiscard]] std::uint32_t Version() const { return version_; }
  [[nodiscard]] bool IsEncrypted() const { return encrypted_; }

 private:
  struct Entry {
    std::uint64_t offset{0};
    std::uint64_t size{0};
    std::uint64_t pathHash{0};
  };

  void* data_{nullptr};
  std::size_t data_size_{0};
  std::uint32_t version_{1};
  bool encrypted_{false};
  std::array<std::uint8_t, 16> salt_{};
  std::array<std::uint8_t, 32> key_{};
  std::map<std::uint64_t, Entry> entries_{};

  bool LoadFromBuffer(const void* buf, std::size_t sz, const std::string& pakPath);
};
