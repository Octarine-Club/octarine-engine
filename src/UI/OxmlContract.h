#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "UI/OxmlParser.h"

enum class ContractFieldType {
  Unknown,
  String,
  Boolean,
  Number,
  Array,
  Table,
};

struct ContractField {
  std::string path{};
  ContractFieldType inferredType{ContractFieldType::String};
  bool conditionalOnly{false};
  bool isTwoWay{false};
  std::string sampleHint{};
  float minVal{0.0f};
  float maxVal{1.0f};
  float step{0.0f};
  bool hasSliderRange{false};
  std::vector<std::string> radioOptions{};
  int firstLine{0};
};

struct ContractParam {
  std::string name{};
  std::string defaultValue{};
  int line{0};
};

struct ContractCallback {
  std::string name{};
  std::string rawExpr{};
  std::string sourceAttr{};
  int line{0};
};

struct UnsupportedAttrRef {
  std::string tag{};
  std::string attr{};
  std::string suggestion{};
  int line{0};
};

struct OxmlContract {
  std::vector<ContractParam> params{};
  std::vector<ContractField> fields{};
  std::vector<ContractCallback> callbacks{};
  std::vector<std::string> refs{};
  std::vector<std::string> slots{};
  std::vector<UnsupportedAttrRef> unsupportedAttrs{};
};

using OxmlResolveUseFn = std::function<std::optional<OxmlDocument>(const std::string& src)>;

OxmlContract InspectOxmlContract(const OxmlDocument& doc,
                                 const std::unordered_map<std::string, OxmlDocument>& preloadedUses = {},
                                 const OxmlResolveUseFn& resolveUse = {});

const char* ContractFieldTypeName(ContractFieldType type);
