vcpkg_download_distfile(ARCHIVE
    URLS "https://www.lua.org/ftp/lua-${VERSION}.tar.gz"
    FILENAME "lua-${VERSION}.tar.gz"
    SHA512 98c5c8978dfdf867e37e9eb3b3ec83dee92d199243b5119505da83895e33f10d43c841be6a7d3b106daba8a0b2bd25fe099ebff8f87831dcc55c79c78b97d8b8
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

# --- Octarine overlay change (only difference from upstream vcpkg lua 5.4.7) -----------------------
# Stock Lua randomizes the string-hash seed per process (time(NULL) + ASLR addresses), which makes
# pairs() iteration order over string-keyed tables differ between runs. Octarine's deterministic
# simulation mode (seeded bot runs, replays, paired A/B balance experiments) needs identical
# iteration order, so we pin the seed. The randomization only defends against hash-flooding DoS
# from untrusted input strings, which does not apply to an embedded game runtime.
file(READ "${SOURCE_PATH}/src/lstate.c" OCTARINE_LSTATE_SRC)
string(FIND "${OCTARINE_LSTATE_SRC}" "#if !defined(luai_makeseed)" OCTARINE_SEED_GUARD_POS)
if(OCTARINE_SEED_GUARD_POS EQUAL -1)
    message(FATAL_ERROR "Octarine lua overlay: luai_makeseed guard not found in lstate.c; re-check patch for this Lua version")
endif()
file(WRITE "${SOURCE_PATH}/src/lstate.c"
    "/* Octarine: fixed hash seed for deterministic table iteration (see overlay portfile). */\n"
    "#define luai_makeseed(L) (cast(unsigned int, 0x0C7A21E5u))\n"
    "${OCTARINE_LSTATE_SRC}")
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
