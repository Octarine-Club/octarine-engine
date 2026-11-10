#include <cmath>
#include <sol/sol.hpp>
#include <string>

#include "Lua/LuaTableSerializer.h"
#include "TestHarness.h"

using octarine::test::Check;
using octarine::test::CheckEq;

namespace {

void TestBasicSerialization() {
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::math);

  lua.script(R"LUA(
    t = {
      score = 100,
      pi = 3.14159,
      is_valid = true,
      name = "Octarine",
      nested = { a = 1, b = 2 }
    }
  )LUA");

  sol::table t = lua["t"];
  std::string out = "return ";
  std::string err;
  bool ok = lua_table_serializer::Serialize(t, out, &err);
  Check(ok, "Basic table serialization succeeds");
  Check(err.empty(), "No error on basic serialization");

  // Load back and assert values
  sol::load_result chunk = lua.load(out);
  Check(chunk.valid(), "Serialized string loads valid Lua chunk");
  sol::table loaded = chunk();
  CheckEq(loaded["score"].get<int>(), 100, "Loaded score matches");
  CheckEq(loaded["name"].get<std::string>(), "Octarine", "Loaded name matches");
  CheckEq(loaded["is_valid"].get<bool>(), true, "Loaded boolean matches");
  CheckEq(loaded["nested"]["a"].get<int>(), 1, "Loaded nested value matches");
}

void TestCycleDetection() {
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::table);

  // Self cycle
  lua.script(R"LUA(
    t_self = { x = 1 }
    t_self.loop = t_self
  )LUA");

  sol::table tSelf = lua["t_self"];
  std::string out;
  std::string err;
  bool okSelf = lua_table_serializer::Serialize(tSelf, out, &err);
  Check(!okSelf, "Self-referential table rejected");
  Check(err.find("cyclic") != std::string::npos, "Error reports cyclic reference on self loop");

  // Mutual cycle: a -> b -> a
  lua.script(R"LUA(
    t_a = { val = "A" }
    t_b = { val = "B" }
    t_a.b = t_b
    t_b.a = t_a
  )LUA");

  sol::table tA = lua["t_a"];
  out.clear();
  err.clear();
  bool okMutual = lua_table_serializer::Serialize(tA, out, &err);
  Check(!okMutual, "Mutual cycle table rejected");
  Check(err.find("cyclic") != std::string::npos, "Error reports cyclic reference on mutual loop");
}

void TestMaxDepthLimit() {
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::table);

  // Create table nested 70 levels deep (> kMaxDepth 64)
  lua.script(R"LUA(
    root = {}
    curr = root
    for i = 1, 70 do
      curr.child = {}
      curr = curr.child
    end
  )LUA");

  sol::table root = lua["root"];
  std::string out;
  std::string err;
  bool ok = lua_table_serializer::Serialize(root, out, &err);
  Check(!ok, "Nesting deeper than kMaxDepth rejected");
  Check(err.find("nesting exceeds") != std::string::npos, "Error reports table nesting exceeds limit");
}

void TestSpecialFloats() {
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::math);

  lua.script(R"LUA(
    t_floats = {
      pos_inf = 1/0,
      neg_inf = -1/0,
      nan_val = 0/0
    }
  )LUA");

  sol::table tFloats = lua["t_floats"];
  std::string out = "return ";
  std::string err;
  bool ok = lua_table_serializer::Serialize(tFloats, out, &err);
  Check(ok, "Serialization of non-finite floats succeeds");

  // Verify non-finite representation uses pure arithmetic literals
  Check(out.find("1/0") != std::string::npos, "Emits 1/0 for +inf");
  Check(out.find("-1/0") != std::string::npos, "Emits -1/0 for -inf");
  Check(out.find("0/0") != std::string::npos, "Emits 0/0 for nan");

  sol::load_result chunk = lua.load(out);
  Check(chunk.valid(), "Serialized non-finite floats reload as valid Lua chunk");
  sol::table loaded = chunk();
  Check(std::isinf(loaded["pos_inf"].get<double>()) && loaded["pos_inf"].get<double>() > 0, "+inf preserved");
  Check(std::isinf(loaded["neg_inf"].get<double>()) && loaded["neg_inf"].get<double>() < 0, "-inf preserved");
  Check(std::isnan(loaded["nan_val"].get<double>()), "NaN preserved");
}

void TestStringAndBinaryEscapes() {
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::table);

  lua.script(R"LUA(
    t_escapes = {
      multiline = "line1\nline2\r\nline3\tquoted: \"hello\"",
      special_chars = "\\backslashes\\ and \000 embedded zero",
      utf8_str = "Octarine 🚀 🎮 👾"
    }
  )LUA");

  sol::table t = lua["t_escapes"];
  std::string out = "return ";
  std::string err;
  bool ok = lua_table_serializer::Serialize(t, out, &err);
  Check(ok, "Serialization of escaped strings succeeds");

  sol::load_result chunk = lua.load(out);
  Check(chunk.valid(), "Serialized escaped strings reload cleanly");
  sol::table loaded = chunk();
  CheckEq(loaded["multiline"].get<std::string>(), t["multiline"].get<std::string>(), "Multiline text round-trips");
  CheckEq(loaded["utf8_str"].get<std::string>(), "Octarine 🚀 🎮 👾", "UTF-8 emoji round-trips");
}

void TestDeterministicKeyOrder() {
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::table);

  // Insert in arbitrary order
  lua.script(R"LUA(
    t_order = {
      zebra = 1,
      apple = 2,
      [10] = "ten",
      [2] = "two",
      [1] = "one",
      mango = 3
    }
  )LUA");

  sol::table t = lua["t_order"];
  std::string out;
  std::string err;
  bool ok = lua_table_serializer::Serialize(t, out, &err);
  Check(ok, "Serialization for deterministic ordering succeeds");

  // Numbers must precede strings: [1], [2], [10] then ["apple"], ["mango"], ["zebra"]
  const size_t pos1 = out.find("[1]");
  const size_t pos2 = out.find("[2]");
  const size_t pos10 = out.find("[10]");
  const size_t posApple = out.find("[\"apple\"]");
  const size_t posMango = out.find("[\"mango\"]");
  const size_t posZebra = out.find("[\"zebra\"]");

  Check(pos1 < pos2, "[1] precedes [2]");
  Check(pos2 < pos10, "[2] precedes [10]");
  Check(pos10 < posApple, R"([10] precedes ["apple"])");
  Check(posApple < posMango, R"(["apple"] precedes ["mango"])");
  Check(posMango < posZebra, R"(["mango"] precedes ["zebra"])");
}

void TestUnsupportedTypes() {
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::table);

  lua.script(R"LUA(
    t_func = {
      action = function() return 42 end
    }
  )LUA");

  sol::table tFunc = lua["t_func"];
  std::string out;
  std::string err;
  bool ok = lua_table_serializer::Serialize(tFunc, out, &err);
  Check(!ok, "Table containing function rejected");
  Check(err.find("unsupported") != std::string::npos, "Error reports unsupported function value");
}

}  // namespace

int main() {
  TestBasicSerialization();
  TestCycleDetection();
  TestMaxDepthLimit();
  TestSpecialFloats();
  TestStringAndBinaryEscapes();
  TestDeterministicKeyOrder();
  TestUnsupportedTypes();
  return octarine::test::ReportSummary("OctarineLuaTableSerializerTest");
}
