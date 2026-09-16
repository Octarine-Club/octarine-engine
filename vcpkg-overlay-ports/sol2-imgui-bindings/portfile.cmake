# Octarine overlay port: the engine's own sol2 ImGui bindings, consumed as a
# prebuilt (binary-cached) static lib instead of an in-tree submodule + add_subdirectory.
# Pinned to a tagged release (vX.Y.Z-<rev>; see the bindings repo CONTRIBUTING.md).
# To bump: change REF to the new tag and update SHA512 to the matching
# https://github.com/Octarine-Club/sol2-imgui-bindings/archive/<tag>.tar.gz hash
# (run `vcpkg install` once with a wrong hash and paste the "Actual hash" it prints).
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Octarine-Club/sol2-imgui-bindings
    REF v1.92.7-1   # X.Y.Z = targeted Dear ImGui version (== this port's version); -<rev> = binding revision
    SHA512 a415b3f94cf0075a56a715f4bac3bd0c2199ed26804b7608d137b72f2d481b19476d2ee5d46ef1a4927c4ab06a99c50abc893172fa00cdac9573d3d0a86f0162
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
