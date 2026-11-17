vcpkg_download_distfile(ARCHIVE
    URLS "https://www.lua.org/ftp/lua-${VERSION}.tar.gz"
    FILENAME "lua-${VERSION}.tar.gz"
    SHA512 1c8cb3d7194732d7685f80a30e0804cb24dc6aee1f8d1383bb9fd5d0699b84c70b823e686555dfa8fd61a32f32c0c687a8419ff4aa285f39d4f3bf3d5cd635c0
)
vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
    PATCHES
        vs2015-impl-c99.patch
        fix-ios-system.patch
)

file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/CMakeLists-cpp.txt" DESTINATION "${SOURCE_PATH}/cpp" RENAME "CMakeLists.txt")

# --- Octarine overlay changes (vs. upstream vcpkg lua 5.5.1) --------------------------------------
# 1. Disable LUA_COMPAT_GLOBAL so `global` is strictly reserved across liblua, luac, and headers.
file(READ "${SOURCE_PATH}/src/luaconf.h" OCTARINE_LUACONF_SRC)
string(REGEX REPLACE "#define[ \t]+LUA_COMPAT_GLOBAL[ \t]+1" "#define LUA_COMPAT_GLOBAL\t0"
       OCTARINE_LUACONF_PATCHED "${OCTARINE_LUACONF_SRC}")
string(FIND "${OCTARINE_LUACONF_PATCHED}" "#define LUA_COMPAT_GLOBAL\t0" OCTARINE_COMPAT_GLOBAL_POS)
if(OCTARINE_COMPAT_GLOBAL_POS EQUAL -1)
    message(FATAL_ERROR "Octarine lua overlay: failed to set LUA_COMPAT_GLOBAL=0 in src/luaconf.h")
endif()
file(WRITE "${SOURCE_PATH}/src/luaconf.h" "${OCTARINE_LUACONF_PATCHED}")

# 2. Stock Lua randomizes the string-hash seed per process (time(NULL) + ASLR addresses), which makes
# pairs() iteration order, initial math.random state, and table.sort pivot selection differ between
# runs. Octarine's deterministic simulation mode needs identical execution across runs, so we pin
# luai_makeseed() in lauxlib.c.
file(READ "${SOURCE_PATH}/src/lauxlib.c" OCTARINE_LAUXLIB_SRC)
string(FIND "${OCTARINE_LAUXLIB_SRC}" "#if !defined(luai_makeseed)" OCTARINE_SEED_GUARD_POS)
if(OCTARINE_SEED_GUARD_POS EQUAL -1)
    message(FATAL_ERROR "Octarine lua overlay: luai_makeseed guard not found in lauxlib.c; re-check patch for this Lua version")
endif()
file(WRITE "${SOURCE_PATH}/src/lauxlib.c"
    "/* Octarine: fixed hash seed for deterministic table iteration, math.random, and table.sort (see overlay portfile). */\n"
    "#define luai_makeseed() ((unsigned int)0x0C7A21E5u)\n"
    "${OCTARINE_LAUXLIB_SRC}")
# ---------------------------------------------------------------------------------------------------

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
  FEATURES
    cpp COMPILE_AS_CPP # Also used in cmake wrapper
    tools INSTALL_TOOLS
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
         ${FEATURE_OPTIONS}
    OPTIONS_DEBUG
        -DSKIP_INSTALL_HEADERS=ON
)
vcpkg_cmake_install()

vcpkg_copy_pdbs()

vcpkg_cmake_config_fixup(PACKAGE_NAME unofficial-lua CONFIG_PATH share/unofficial-lua)

if("cpp" IN_LIST FEATURES)
    vcpkg_cmake_config_fixup(PACKAGE_NAME unofficial-lua-cpp CONFIG_PATH "share/unofficial-lua-cpp")
endif()

if ("tools" IN_LIST FEATURES)
    vcpkg_copy_tools(TOOL_NAMES lua luac SEARCH_DIR "${CURRENT_PACKAGES_DIR}/tools/${PORT}")
endif()

if(VCPKG_LIBRARY_LINKAGE STREQUAL dynamic)
    if(VCPKG_TARGET_IS_WINDOWS)
        vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/luaconf.h" "defined(LUA_BUILD_AS_DLL)" "1")
    endif()
endif()

# Suitable for old version
configure_file("${CMAKE_CURRENT_LIST_DIR}/vcpkg-cmake-wrapper.cmake.in"  "${CURRENT_PACKAGES_DIR}/share/${PORT}/vcpkg-cmake-wrapper.cmake" @ONLY)
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/COPYRIGHT" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
