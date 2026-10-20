#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace octarine::crypto {

constexpr uint64_t kFnv1aOffsetBasis64 = 0xcbf29ce484222325ULL;
constexpr uint64_t kFnv1aPrime64 = 0x100000001b3ULL;

// Computes 64-bit FNV-1a hash over raw bytes.
constexpr uint64_t Fnv1a64(const void* data, size_t size, uint64_t basis = kFnv1aOffsetBasis64) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  uint64_t hash = basis;
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= kFnv1aPrime64;
  }
  return hash;
}

// Computes 64-bit FNV-1a hash over a string view.
constexpr uint64_t Fnv1a64(std::string_view s, uint64_t basis = kFnv1aOffsetBasis64) {
  uint64_t hash = basis;
  for (const char c : s) {
    hash ^= static_cast<uint8_t>(c);
    hash *= kFnv1aPrime64;
  }
  return hash;
}

// Normalizes a project-relative path (forward slashes, no leading `./` or `/`, lowercased)
// and computes its 64-bit FNV-1a hash.
inline uint64_t HashPath(std::string_view path) {
  while (path.starts_with("./") || path.starts_with(".\\")) {
    path.remove_prefix(2);
  }
  while (path.starts_with('/') || path.starts_with('\\')) {
    path.remove_prefix(1);
  }

  uint64_t hash = kFnv1aOffsetBasis64;
  for (const char ch : path) {
    char normalized = ch;
    if (normalized == '\\') {
      normalized = '/';
    }
    normalized = static_cast<char>(std::tolower(static_cast<unsigned char>(normalized)));
    hash ^= static_cast<uint8_t>(normalized);
    hash *= kFnv1aPrime64;
  }
  return hash;
}

}  // namespace octarine::crypto
