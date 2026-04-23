vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL "https://github.com/libsixel/libsixel.git"
    REF 37026b01a0bd38634ae0a8c5017bd4671101fe08
    HEAD_REF master
)

configure_file("${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" "${SOURCE_PATH}/CMakeLists.txt" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/config.h.in" "${SOURCE_PATH}/config.h.in" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/tty_win32.c" "${SOURCE_PATH}/tty_win32.c" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/unofficial-libsixel-config.cmake.in" "${SOURCE_PATH}/unofficial-libsixel-config.cmake.in" COPYONLY)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DUPSTREAM_VERSION=${VERSION}
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(CONFIG_PATH share/unofficial-libsixel PACKAGE_NAME unofficial-libsixel)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
