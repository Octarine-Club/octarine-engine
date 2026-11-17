set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

get_filename_component(_octarine_cmake_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(VCPKG_CMAKE_CONFIGURE_OPTIONS
    "-DCMAKE_USER_MAKE_RULES_OVERRIDE=${_octarine_cmake_dir}/gcc15-slp-workaround.cmake"
)
unset(_octarine_cmake_dir)
