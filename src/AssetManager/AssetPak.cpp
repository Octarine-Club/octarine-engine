#include "AssetManager/AssetPak.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "AssetManager/AssetCatalog.h"
#include "Crypto/ChaCha20.h"
#include "Crypto/Hash.h"
#include "Crypto/KeyDerivation.h"
#include "Crypto/LuaBytecodeScrambler.h"
#include "General/Logger.h"

namespace {
constexpr char kMagic[4] = {'O', 'C', 'P', 'K'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kFlagEncrypted = 1;

#pragma pack(push, 1)
struct PakHeader {
  char magic[4];
  std::uint32_t version;
  std::uint64_t tocOffset;
  std::uint32_t tocCount;
  std::uint32_t flags;
  std::uint8_t salt[16];
  std::uint8_t tocMac[16];
};

struct PakTocEntry {
  std::uint64_t pathHash;
  std::uint64_t offset;
  std::uint64_t size;
};
#pragma pack(pop)

static_assert(sizeof(PakHeader) == 56, "PakHeader must be exactly 56 bytes on disk");
static_assert(sizeof(PakTocEntry) == 24, "PakTocEntry must be exactly 24 bytes on disk");

std::array<std::uint8_t, 16> ComputeHeaderAndTocMac(const PakHeader& header, const PakTocEntry* tocEntries,
                                                    std::size_t tocCount, std::span<const std::uint8_t, 32> key) {
  const std::size_t metaSize = 24;  // magic(4) + version(4) + tocOffset(8) + tocCount(4) + flags(4)
  const std::size_t tocBytes = tocCount * sizeof(PakTocEntry);
  std::vector<std::uint8_t> macInput(metaSize + tocBytes);
  std::memcpy(macInput.data(), &header, metaSize);
  if (tocBytes > 0 && tocEntries != nullptr) {
    std::memcpy(macInput.data() + metaSize, tocEntries, tocBytes);
  }
  std::span<const std::uint8_t, 16> saltSpan(header.salt, 16);
  return octarine::crypto::ComputeKeyedMac128(key, saltSpan, macInput.data(), macInput.size());
}

struct PakStreamContext {
  const uint8_t* baseData{nullptr};
  uint64_t entryOffset{0};
  uint64_t entrySize{0};
  uint64_t cursor{0};
  octarine::crypto::ChaCha20 cipher;
};

Sint64 SDLCALL PakStreamSize(void* userdata) {
  const auto* ctx = static_cast<const PakStreamContext*>(userdata);
  return static_cast<Sint64>(ctx->entrySize);
}

Sint64 SDLCALL PakStreamSeek(void* userdata, Sint64 offset, SDL_IOWhence whence) {
  auto* ctx = static_cast<PakStreamContext*>(userdata);
  Sint64 nextPos = 0;
  switch (whence) {
    case SDL_IO_SEEK_SET:
      nextPos = offset;
      break;
    case SDL_IO_SEEK_CUR:
      nextPos = static_cast<Sint64>(ctx->cursor) + offset;
      break;
    case SDL_IO_SEEK_END:
      nextPos = static_cast<Sint64>(ctx->entrySize) + offset;
      break;
  }
  if (nextPos < 0 || nextPos > static_cast<Sint64>(ctx->entrySize)) {
    SDL_SetError("PakStreamSeek: offset out of range");
    return -1;
  }
  ctx->cursor = static_cast<uint64_t>(nextPos);
  return nextPos;
}

size_t SDLCALL PakStreamRead(void* userdata, void* ptr, size_t size, SDL_IOStatus* status) {
  auto* ctx = static_cast<PakStreamContext*>(userdata);
  if (ctx->cursor >= ctx->entrySize) {
    if (status != nullptr) *status = SDL_IO_STATUS_EOF;
    return 0;
  }
  const auto available = static_cast<size_t>(ctx->entrySize - ctx->cursor);
  const size_t toRead = std::min(size, available);
  ctx->cipher.Crypt(ctx->baseData + ctx->entryOffset + ctx->cursor, static_cast<uint8_t*>(ptr), toRead, ctx->cursor);
  ctx->cursor += toRead;
  if (toRead < size && status != nullptr) {
    *status = SDL_IO_STATUS_EOF;
  }
  return toRead;
}

bool SDLCALL PakStreamClose(void* userdata) {
  delete static_cast<PakStreamContext*>(userdata);
  return true;
}

SDL_IOStream* CreatePakStream(const uint8_t* baseData, uint64_t offset, uint64_t size, std::span<const uint8_t, 32> key,
                              std::span<const uint8_t, 12> nonce) {
  auto* ctx = new PakStreamContext{};
  ctx->baseData = baseData;
  ctx->entryOffset = offset;
  ctx->entrySize = size;
  ctx->cursor = 0;
  ctx->cipher.SetKey(key);
  ctx->cipher.SetNonce(nonce, 0);

  SDL_IOStreamInterface iface{};
  SDL_INIT_INTERFACE(&iface);
  iface.size = &PakStreamSize;
  iface.seek = &PakStreamSeek;
  iface.read = &PakStreamRead;
  iface.close = &PakStreamClose;

  SDL_IOStream* io = SDL_OpenIO(&iface, ctx);
  if (io == nullptr) {
    delete ctx;
  }
  return io;
}

std::string MakeRelPath(const std::string& fullPath, const std::string& basePath) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path rel = fs::relative(fs::path(fullPath), fs::path(basePath), ec);
  if (ec || rel.empty()) {
    rel = fs::path(fullPath);
  }
  return rel.generic_string();
}

std::array<std::uint8_t, 16> GenerateSalt() {
  std::array<std::uint8_t, 16> salt{};
  const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  const auto perf = SDL_GetPerformanceCounter();
  const uint64_t h1 = octarine::crypto::Fnv1a64(&now, sizeof(now));
  const uint64_t h2 = octarine::crypto::Fnv1a64(&perf, sizeof(perf), h1);
  std::memcpy(salt.data(), &h1, 8);
  std::memcpy(salt.data() + 8, &h2, 8);
  return salt;
}
}  // namespace

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
AssetPak::AssetPak(AssetPak&& other) noexcept
    : data_(other.data_),
      data_size_(other.data_size_),
      version_(other.version_),
      encrypted_(other.encrypted_),
      salt_(other.salt_),
      key_(other.key_),
      entries_(std::move(other.entries_)) {
  other.data_ = nullptr;
  other.data_size_ = 0;
}

AssetPak& AssetPak::operator=(AssetPak&& other) noexcept {
  if (this != &other) {
    if (data_ != nullptr) SDL_free(data_);
    data_ = other.data_;
    data_size_ = other.data_size_;
    version_ = other.version_;
    encrypted_ = other.encrypted_;
    salt_ = other.salt_;
    key_ = other.key_;
    entries_ = std::move(other.entries_);
    other.data_ = nullptr;
    other.data_size_ = 0;
  }
  return *this;
}

AssetPak::~AssetPak() {
  if (data_ != nullptr) {
    SDL_free(data_);
    data_ = nullptr;
  }
}

bool AssetPak::LoadFromBuffer(const void* buf, std::size_t sz, const std::string& pakPath) {
  if (sz < sizeof(PakHeader)) {
    Logger::Error("AssetPak::Open: " + pakPath + " is shorter than pak header");
    return false;
  }
  PakHeader header{};
  std::memcpy(&header, buf, sizeof(header));
  if (std::memcmp(header.magic, kMagic, 4) != 0) {
    Logger::Error("AssetPak::Open: " + pakPath + " has wrong magic (not an OCPK file)");
    return false;
  }
  if (header.version != kVersion || (header.flags & kFlagEncrypted) == 0) {
    Logger::Error("AssetPak::Open: " + pakPath + " is unsupported or unencrypted OCPK v" +
                  std::to_string(header.version));
    return false;
  }
  if (header.tocOffset > sz || header.tocCount > (sz - header.tocOffset) / sizeof(PakTocEntry)) {
    Logger::Error("AssetPak::Open: " + pakPath + " TOC truncated or past end of file");
    return false;
  }

  const std::size_t expectedTocBytes = static_cast<std::size_t>(header.tocCount) * sizeof(PakTocEntry);
  version_ = header.version;
  encrypted_ = true;
  std::memcpy(salt_.data(), header.salt, sizeof(header.salt));
  key_ = octarine::crypto::GetActivePackageKey();
  entries_.clear();

  std::vector<PakTocEntry> toc(header.tocCount);
  if (expectedTocBytes > 0) {
    std::memcpy(toc.data(), static_cast<const std::uint8_t*>(buf) + header.tocOffset, expectedTocBytes);
    std::array<std::uint8_t, 12> tocNonce{};
    std::memcpy(tocNonce.data(), salt_.data(), 12);
    octarine::crypto::ChaCha20 tocCipher(key_, tocNonce, 0);
    tocCipher.XorInPlace(reinterpret_cast<uint8_t*>(toc.data()), expectedTocBytes, 0);
  }

  const auto expectedMac = ComputeHeaderAndTocMac(header, toc.data(), toc.size(), key_);
  if (std::memcmp(expectedMac.data(), header.tocMac, sizeof(header.tocMac)) != 0) {
    Logger::Error("AssetPak::Open: header/TOC integrity verification failed in " + pakPath);
    return false;
  }

  for (const auto& item : toc) {
    if (item.size > header.tocOffset || item.offset > header.tocOffset - item.size) {
      Logger::Error("AssetPak::Open: entry overlaps TOC in " + pakPath);
      entries_.clear();
      return false;
    }
    entries_.emplace(item.pathHash, Entry{item.offset, item.size, item.pathHash});
  }
  return true;
}

bool AssetPak::Open(const std::string& pakPath) {
  if (data_ != nullptr) {
    SDL_free(data_);
    data_ = nullptr;
    data_size_ = 0;
    entries_.clear();
  }

  std::size_t sz = 0;
  void* buf = SDL_LoadFile(pakPath.c_str(), &sz);
  if (buf == nullptr) {
    Logger::Warn("AssetPak::Open: SDL_LoadFile failed for " + pakPath + ": " + std::string(SDL_GetError()));
    return false;
  }

  if (!LoadFromBuffer(buf, sz, pakPath)) {
    SDL_free(buf);
    entries_.clear();
    return false;
  }

  data_ = buf;
  data_size_ = sz;
  Logger::Info("AssetPak::Open: loaded " + std::to_string(entries_.size()) + " encrypted entries from " + pakPath);
  return true;
}

bool AssetPak::Contains(const std::string& relPath) const {
  if (data_ == nullptr) return false;
  const uint64_t hash = octarine::crypto::HashPath(relPath);
  return entries_.find(hash) != entries_.end();
}

SDL_IOStream* AssetPak::OpenIO(const std::string& relPath) const {
  if (data_ == nullptr) return nullptr;

  const uint64_t hash = octarine::crypto::HashPath(relPath);
  const auto it = entries_.find(hash);
  if (it == entries_.end()) {
    return nullptr;
  }

  std::array<std::uint8_t, 12> nonce{};
  std::memcpy(nonce.data(), &it->second.pathHash, 8);
  std::memcpy(nonce.data() + 8, salt_.data() + 12, 4);
  return CreatePakStream(static_cast<const uint8_t*>(data_), it->second.offset, it->second.size, key_, nonce);
}

namespace {
bool WriteFileEntry(std::ofstream& out, const std::string& sourcePath, uint64_t pathHash,
                    std::span<const uint8_t, 32> key, std::span<const uint8_t, 16> salt, PakTocEntry& outEntry) {
  std::ifstream in(sourcePath, std::ios::binary);
  if (!in) {
    Logger::Error("AssetPak::Pack: failed to read " + sourcePath);
    return false;
  }

  const auto offset = static_cast<uint64_t>(out.tellp());
  std::vector<uint8_t> fileData((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

  // Scramble compiled Lua 5.4 bytecode if detected before packing
  static constexpr uint8_t kLuaMagic[] = {0x1B, 'L', 'u', 'a', 0x54};
  if (fileData.size() >= 34 && std::memcmp(fileData.data(), kLuaMagic, 5) == 0) {
    uint64_t seed = 0;
    std::memcpy(&seed, key.data(), sizeof(seed));
    octarine::crypto::ScrambleLua54Bytecode(fileData.data(), fileData.size(), seed);
  }

  std::array<uint8_t, 12> nonce{};
  std::memcpy(nonce.data(), &pathHash, 8);
  std::memcpy(nonce.data() + 8, salt.data() + 12, 4);
  octarine::crypto::ChaCha20 cipher(key, nonce, 0);
  cipher.XorInPlace(fileData.data(), fileData.size(), 0);

  out.write(reinterpret_cast<const char*>(fileData.data()), static_cast<std::streamsize>(fileData.size()));

  outEntry.pathHash = pathHash;
  outEntry.offset = offset;
  outEntry.size = fileData.size();
  return true;
}
}  // namespace

bool AssetPak::Pack(const AssetCatalog& catalog, const std::string& outPath, const std::string& basePath,
                    const std::vector<std::string>& extraFiles,
                    const std::map<std::string, std::string>& pathOverrides) {
  std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
  if (!out) {
    Logger::Error("AssetPak::Pack: failed to open " + outPath + " for writing");
    return false;
  }

  const auto salt = GenerateSalt();
  const auto key = octarine::crypto::GetActivePackageKey();

  PakHeader header{};
  std::memcpy(header.magic, kMagic, 4);
  header.version = kVersion;
  header.flags = kFlagEncrypted;
  std::memcpy(header.salt, salt.data(), sizeof(header.salt));
  out.write(reinterpret_cast<const char*>(&header), sizeof(header));

  std::vector<PakTocEntry> staged;
  staged.reserve(catalog.Size() + extraFiles.size());

  for (const auto& [id, entry] : catalog.Entries()) {
    const std::string relPath = MakeRelPath(entry.fullPath, basePath);
    std::string sourcePath = entry.fullPath;
    if (auto it = pathOverrides.find(relPath); it != pathOverrides.end()) {
      sourcePath = it->second;
    }
    PakTocEntry tocEntry{};
    if (!WriteFileEntry(out, sourcePath, octarine::crypto::HashPath(relPath), key, salt, tocEntry)) {
      out.close();
      std::error_code ec;
      std::filesystem::remove(outPath, ec);
      return false;
    }
    staged.push_back(tocEntry);
  }

  for (const std::string& extraPath : extraFiles) {
    const std::string rel = MakeRelPath(extraPath, basePath);
    const uint64_t hash = octarine::crypto::HashPath(rel);
    if (std::any_of(staged.begin(), staged.end(), [hash](const auto& s) { return s.pathHash == hash; })) {
      continue;
    }
    std::string sourcePath = extraPath;
    if (const auto it = pathOverrides.find(rel); it != pathOverrides.end()) {
      sourcePath = it->second;
    }
    PakTocEntry tocEntry{};
    if (!WriteFileEntry(out, sourcePath, hash, key, salt, tocEntry)) {
      out.close();
      std::error_code ec;
      std::filesystem::remove(outPath, ec);
      return false;
    }
    staged.push_back(tocEntry);
  }

  // Encrypt and write TOC, then compute MAC over header metadata + plaintext TOC
  const auto tocOffset = static_cast<uint64_t>(out.tellp());
  header.tocOffset = tocOffset;
  header.tocCount = static_cast<std::uint32_t>(staged.size());
  const auto mac = ComputeHeaderAndTocMac(header, staged.data(), staged.size(), key);
  std::memcpy(header.tocMac, mac.data(), sizeof(header.tocMac));

  std::vector<PakTocEntry> encToc = staged;
  if (!encToc.empty()) {
    std::array<std::uint8_t, 12> tocNonce{};
    std::memcpy(tocNonce.data(), salt.data(), 12);
    octarine::crypto::ChaCha20 tocCipher(key, tocNonce, 0);
    tocCipher.XorInPlace(reinterpret_cast<uint8_t*>(encToc.data()), encToc.size() * sizeof(PakTocEntry), 0);
    out.write(reinterpret_cast<const char*>(encToc.data()),
              static_cast<std::streamsize>(encToc.size() * sizeof(PakTocEntry)));
  }

  out.seekp(0, std::ios::beg);
  out.write(reinterpret_cast<const char*>(&header), sizeof(header));
  out.close();

  if (!out) {
    Logger::Error("AssetPak::Pack: write error finalizing " + outPath);
    std::error_code ec;
    std::filesystem::remove(outPath, ec);
    return false;
  }
  Logger::Info("AssetPak::Pack: wrote " + std::to_string(staged.size()) + " encrypted entries to " + outPath);
  return true;
}
