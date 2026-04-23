set(VCPKG_POLICY_DLLS_WITHOUT_LIBS enabled)

if(NOT VCPKG_TARGET_IS_WINDOWS)
    message(FATAL_ERROR "${PORT} overlay currently supports Windows only.")
endif()

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    message(FATAL_ERROR "${PORT} overlay packages the upstream shared libvips bundle only.")
endif()

vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/libvips/build-win64-mxe/releases/download/v8.18.2/vips-dev-x64-all-8.18.2.zip"
    FILENAME "vips-dev-x64-all-8.18.2.zip"
    SHA512 64030e14ffd1a390568ad0243d14ad82c9050dcfe0c85026d69618b71f32655de95dcba7b33144e9449f938e78205274badebeb700434753bc9ff31f61db9b42
)

vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
)

file(GLOB vips_header "${SOURCE_PATH}/*/include/vips/vips.h")
if(NOT vips_header)
    message(FATAL_ERROR "Could not locate include/vips/vips.h in the libvips bundle.")
endif()

list(GET vips_header 0 vips_header)
get_filename_component(vips_include_dir "${vips_header}" DIRECTORY)
get_filename_component(include_root "${vips_include_dir}" DIRECTORY)
get_filename_component(bundle_root "${include_root}" DIRECTORY)

file(COPY "${include_root}/" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(COPY "${bundle_root}/lib/" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
file(COPY "${bundle_root}/bin/" DESTINATION "${CURRENT_PACKAGES_DIR}/bin")

if(NOT VCPKG_BUILD_TYPE)
    file(COPY "${include_root}/" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/include")
    file(COPY "${bundle_root}/lib/" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
    file(COPY "${bundle_root}/bin/" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/bin")
endif()

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/unofficial-vips")
configure_file("${CMAKE_CURRENT_LIST_DIR}/unofficial-vips-config.cmake" "${CURRENT_PACKAGES_DIR}/share/unofficial-vips/unofficial-vips-config.cmake" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/unofficial-vips-config-version.cmake" "${CURRENT_PACKAGES_DIR}/share/unofficial-vips/unofficial-vips-config-version.cmake" COPYONLY)
file(COPY "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${bundle_root}/LICENSE")
