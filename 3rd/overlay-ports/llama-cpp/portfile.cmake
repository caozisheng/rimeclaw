vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ggml-org/llama.cpp
    REF b${VERSION}
    SHA512 71996a3ca12e15257292ad26dcc78c9593952d0d73d91b5f069dbeddab31d754eb4734ab0a6a6f93fbe2d96db58593ece9a2b296780fa8ab19c8c649830df7b8
    HEAD_REF master
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DGGML_CCACHE=OFF
        -DLLAMA_ALL_WARNINGS=OFF
        -DLLAMA_BUILD_TESTS=OFF
        -DLLAMA_BUILD_EXAMPLES=OFF
        -DLLAMA_BUILD_SERVER=OFF
        -DLLAMA_BUILD_TOOLS=OFF
        -DLLAMA_CURL=OFF
)

vcpkg_cmake_install()

# ggml-config.cmake is generated in the build tree but NOT installed by cmake.
# Install it directly to share/ggml/ (vcpkg convention) and fix the prefix path.
file(INSTALL "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/ggml/ggml-config.cmake"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/ggml")
vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/ggml/ggml-config.cmake"
    [[get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)]]
    [[get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)]])

vcpkg_cmake_config_fixup(PACKAGE_NAME llama CONFIG_PATH lib/cmake/llama)
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
