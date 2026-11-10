# Octarine overlay port: the engine's own sol2 ImGui bindings, consumed as a
# prebuilt (binary-cached) static lib instead of an in-tree submodule + add_subdirectory.
# Pinned to a tagged release (vX.Y.Z-<rev>; see the bindings repo CONTRIBUTING.md).
# To bump: change REF to the new tag and update SHA512 to the matching
# https://github.com/Octarine-Club/sol2-imgui-bindings/archive/<tag>.tar.gz hash
# (run `vcpkg install` once with a wrong hash and paste the "Actual hash" it prints).
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Octarine-Club/sol2-imgui-bindings
    REF v1.93.0-1   # X.Y.Z = targeted Dear ImGui version (== this port's version); -<rev> = binding revision
    SHA512 21b2a1c2e5ce8903b49e9565f72d7c622a99cb5e5deb095003f70563eaf98d9a14be21210f536250786fdecb76b43ba2b9dce639959e5eb44d3df33821c0c650
    HEAD_REF main
)

# The bindings' CMake ships install() + EXPORT + a find_package config (gated by
# SOL2_IMGUI_BINDINGS_INSTALL, on by default when standalone). Tests stay off.
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DSOL2_IMGUI_BINDINGS_INSTALL=ON
        -DSOL2_IMGUI_BINDINGS_BUILD_TESTS=OFF
)
vcpkg_cmake_install()

# Bindings install both sol2-imgui-bindings (targets) and sol2_ImGui_Bindings (compat wrapper). Relocate both to share/.
vcpkg_cmake_config_fixup(PACKAGE_NAME sol2-imgui-bindings CONFIG_PATH lib/cmake/sol2-imgui-bindings DO_NOT_DELETE_PARENT_CONFIG_PATH)
vcpkg_cmake_config_fixup(PACKAGE_NAME sol2_ImGui_Bindings CONFIG_PATH lib/cmake/sol2_ImGui_Bindings)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
