#!/usr/bin/env python3
"""Enforces architectural boundaries, static layering, genre purity, and CMake/doc parity.

Run with: python3 scripts/check_architecture.py
Exits 0 on success, 1 on any violation.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = REPO_ROOT / "src"
TESTS_DIR = REPO_ROOT / "tests"

LAYER_ORDER = [
    "octarine_core",
    "octarine_assets",
    "octarine_renderer",
    "octarine_systems",
    "octarine_lua",
    "octarine_engine",
]
LAYER_RANK = {name: i for i, name in enumerate(LAYER_ORDER)}


def get_layer(rel_path: str) -> str:
    p = rel_path.replace("\\", "/")
    if any(
        p.startswith(x)
        for x in [
            "General/",
            "ECS/",
            "Components/",
            "EventBus/",
            "Events/",
            "Project/",
            "Game/GameConfig",
            "Game/UserSettings",
            "Game/EngineOptions",
            "Game/EngineContext",
            "Crypto/",
            "UI/OssParser",
            "UI/OxmlParser",
            "Debug/DebugMenuRegistry",
            "Debug/SystemProfilerTracker",
        ]
    ):
        return "octarine_core"
    if p.startswith("AssetManager/"):
        return "octarine_assets"
    if p.startswith("Audio/"):
        return "octarine_systems"
    if p.startswith("Renderer/"):
        return "octarine_renderer"
    if p.startswith("Systems/RenderDebugGUISystem") or p.startswith("Systems/TilemapDebugPanel"):
        return "octarine_engine"
    if p.startswith("Systems/") or p.startswith("Tilemap/"):
        if "LuaBinding" in p or p == "Systems/ScriptSystem.h":
            return "octarine_lua"
        return "octarine_systems"
    if p.startswith("Lua/") or p.startswith("UI/"):
        return "octarine_lua"
    if (
        p.startswith("Engine/")
        or p.startswith("Game/")
        or p.startswith("Debug/")
        or p.startswith("Dev/")
        or p == "Main.cpp"
    ):
        return "octarine_engine"
    return "unknown"


def check_layer_includes() -> list[str]:
    inc_re = re.compile(r'^\s*#\s*include\s*["<]([^">]+)[">]')
    violations = []

    for file_path in sorted(SRC_DIR.rglob("*")):
        if not file_path.is_file() or file_path.suffix not in (".h", ".hpp", ".cpp"):
            continue
        rel = file_path.relative_to(SRC_DIR).as_posix()
        src_layer = get_layer(rel)
        if src_layer == "unknown":
            violations.append(f"{rel}: file belongs to unknown architectural layer")
            continue

        for line_num, line in enumerate(
            file_path.read_text(encoding="utf-8", errors="ignore").splitlines(), 1
        ):
            match = inc_re.match(line)
            if not match:
                continue
            target = match.group(1)
            target_path = SRC_DIR / target
            if not target_path.exists():
                continue

            tgt_layer = get_layer(target)
            if tgt_layer == "unknown":
                violations.append(
                    f"{rel}:{line_num}: includes '{target}' from unknown architectural layer"
                )
                continue

            src_rank = LAYER_RANK.get(src_layer, 0)
            tgt_rank = LAYER_RANK.get(tgt_layer, 0)

            if tgt_rank > src_rank:
                violations.append(
                    f"{rel}:{line_num}: layer '{src_layer}' cannot include '{target}' from higher layer '{tgt_layer}'"
                )

    return violations


def check_cmake_parity() -> list[str]:
    violations = []
    src_cmake_text = (SRC_DIR / "CMakeLists.txt").read_text(encoding="utf-8")

    # Platform-specific or standalone executable files not registered in static libs
    ignored_src_cpp = {"General/SDL3_Linux_Stubs.cpp", "Main.cpp"}

    for cpp_file in sorted(SRC_DIR.rglob("*.cpp")):
        rel = cpp_file.relative_to(SRC_DIR).as_posix()
        if rel in ignored_src_cpp:
            continue
        if rel not in src_cmake_text:
            violations.append(f"src/{rel} is missing from src/CMakeLists.txt")

    tests_cmake_text = (TESTS_DIR / "CMakeLists.txt").read_text(encoding="utf-8")
    for test_file in sorted(TESTS_DIR.glob("*Test.cpp")):
        if test_file.name not in tests_cmake_text:
            violations.append(f"tests/{test_file.name} is missing from tests/CMakeLists.txt")

    return violations


def strip_comments_and_strings(code: str) -> str:
    # Remove raw string literals R"(...)"
    code = re.sub(r'R"([^(]*)\(.*?\)\1"', "", code, flags=re.DOTALL)
    # Remove double-quoted string literals
    code = re.sub(r'"(\\.|[^"\\])*"', "", code)
    # Remove single-quoted character literals
    code = re.sub(r"'(\\.|[^'\\])*'", "", code)
    # Remove block comments
    code = re.sub(r"/\*.*?\*/", "", code, flags=re.DOTALL)
    # Remove line comments
    code = re.sub(r"//.*$", "", code, flags=re.MULTILINE)
    return code


def check_genre_purity() -> list[str]:
    forbidden_tokens = {
        "player_health",
        "enemy",
        "enemies",
        "weapon",
        "bullet",
        "inventory",
        "mana",
        "stamina",
        "quest",
        "dungeon",
        "loot",
        "xp",
    }
    violations = []

    for file_path in sorted(SRC_DIR.rglob("*")):
        if not file_path.is_file() or file_path.suffix not in (".h", ".hpp", ".cpp"):
            continue
        rel = file_path.relative_to(REPO_ROOT).as_posix()
        raw_text = file_path.read_text(encoding="utf-8", errors="ignore")
        stripped = strip_comments_and_strings(raw_text)

        for line_num, line in enumerate(stripped.splitlines(), 1):
            words = set(re.findall(r"\b[a-zA-Z_]\w*\b", line.lower()))
            intersect = words.intersection(forbidden_tokens)
            if intersect:
                violations.append(
                    f"{rel}:{line_num}: forbidden genre-specific token(s) {sorted(intersect)} found in code"
                )

    return violations


def check_doc_parity() -> list[str]:
    violations = []
    docs_to_check = [
        REPO_ROOT / "AGENTS.md",
        REPO_ROOT / "CONTEXT.md",
    ]
    docs_to_check.extend((REPO_ROOT / ".agents" / "skills").rglob("*.md"))

    for doc in docs_to_check:
        if not doc.exists():
            continue
        text = doc.read_text(encoding="utf-8", errors="ignore")
        rel = doc.relative_to(REPO_ROOT).as_posix()
        if "octarine_editor" in text:
            violations.append(f"{rel}: references removed library 'octarine_editor'")
        if "ninja -C build " in text:
            violations.append(f"{rel}: references non-preset build dir 'ninja -C build'")

    return violations


def main() -> int:
    print("[check_architecture] Auditing layer include boundaries...")
    inc_violations = check_layer_includes()

    print("[check_architecture] Auditing CMake source & test parity...")
    cmake_violations = check_cmake_parity()

    print("[check_architecture] Auditing genre-agnostic purity in src/...")
    genre_violations = check_genre_purity()

    print("[check_architecture] Auditing agent documentation & skill parity...")
    doc_violations = check_doc_parity()

    total_violations = len(inc_violations) + len(cmake_violations) + len(genre_violations) + len(doc_violations)

    if total_violations == 0:
        print("[check_architecture] PASS: All architectural invariants verified cleanly.")
        return 0

    print(f"\n[check_architecture] FAIL: Found {total_violations} violation(s):\n", file=sys.stderr)
    for v in inc_violations:
        print(f"  [layer-include] {v}", file=sys.stderr)
    for v in cmake_violations:
        print(f"  [cmake-parity]  {v}", file=sys.stderr)
    for v in genre_violations:
        print(f"  [genre-purity]  {v}", file=sys.stderr)
    for v in doc_violations:
        print(f"  [doc-parity]    {v}", file=sys.stderr)

    return 1


if __name__ == "__main__":
    sys.exit(main())
