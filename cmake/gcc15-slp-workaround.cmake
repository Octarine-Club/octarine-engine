# Work around GCC 15.0–15.2 x86_64 AVX2+FMA SLP tree-vectorizer register-aliasing bug
# (miscompiles 2x2 rotation matrices such as SDL3's SDL_RenderTextureRotated into
# `vfmaddsub132ps %ymm0, %ymm1, %ymm0`, squaring centered vertex offsets; fixed in GCC 15.3).
if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU"
        AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "15.0"
        AND CMAKE_C_COMPILER_VERSION VERSION_LESS "15.3")
        string(APPEND CMAKE_C_FLAGS_INIT " -fno-tree-slp-vectorize")
        string(APPEND CMAKE_C_FLAGS " -fno-tree-slp-vectorize")
    endif ()
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "15.0"
        AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "15.3")
        string(APPEND CMAKE_CXX_FLAGS_INIT " -fno-tree-slp-vectorize")
        string(APPEND CMAKE_CXX_FLAGS " -fno-tree-slp-vectorize")
    endif ()
endif ()
