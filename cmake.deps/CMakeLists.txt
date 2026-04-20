cmake_minimum_required(VERSION 3.21)
project(dcat_deps LANGUAGES NONE)

if(NOT WIN32)
  message(FATAL_ERROR "cmake.deps currently targets Windows/MSVC first.")
endif()

find_program(GIT_EXECUTABLE git REQUIRED)

set(DCAT_VCPKG_REPOSITORY "https://github.com/microsoft/vcpkg.git" CACHE STRING "vcpkg repository URL")
set(DCAT_VCPKG_REF "master" CACHE STRING "vcpkg ref/tag/branch to checkout")
set(DCAT_VCPKG_ROOT "${CMAKE_BINARY_DIR}/vcpkg" CACHE PATH "Path where vcpkg will be cloned")
set(DCAT_VCPKG_TRIPLET "x64-windows" CACHE STRING "vcpkg triplet used for dependency builds")
set(DEPS_INSTALL_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed" CACHE PATH "Installation root for vcpkg outputs")
option(DCAT_BUNDLE_VIPS "Download libvips prebuilt binaries if vcpkg has no vips port" ON)
set(DCAT_VIPS_VERSION "8.18.2" CACHE STRING "libvips build-win64-mxe version used for fallback bundle")
set(
  DCAT_VIPS_X64_ALL_SHA256
  "aec9b8d5e79c06aade9fd51224570c87687675896d4da335955047c211d40e01"
  CACHE STRING
  "SHA256 for vips-dev-x64-all archive"
)
set(
  DCAT_VCPKG_PACKAGES
  "assimp;cglm;pkgconf;shaderc;vulkan"
  CACHE STRING
  "Semicolon-separated vcpkg package list (core set without vips)"
)

if(NOT EXISTS "${DCAT_VCPKG_ROOT}/.git")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" clone "${DCAT_VCPKG_REPOSITORY}" "${DCAT_VCPKG_ROOT}"
    RESULT_VARIABLE clone_result
  )
  if(NOT clone_result EQUAL 0)
    message(FATAL_ERROR "Failed to clone vcpkg from ${DCAT_VCPKG_REPOSITORY}")
  endif()
endif()

if(NOT DCAT_VCPKG_REF STREQUAL "")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${DCAT_VCPKG_ROOT}" fetch --depth 1 origin "${DCAT_VCPKG_REF}"
    RESULT_VARIABLE fetch_result
  )
  if(fetch_result EQUAL 0)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" -C "${DCAT_VCPKG_ROOT}" checkout --force FETCH_HEAD
      RESULT_VARIABLE checkout_result
    )
    if(NOT checkout_result EQUAL 0)
      message(FATAL_ERROR "Failed to checkout vcpkg ref: ${DCAT_VCPKG_REF}")
    endif()
  else()
    message(FATAL_ERROR "Failed to fetch vcpkg ref: ${DCAT_VCPKG_REF}")
  endif()
endif()

if(NOT EXISTS "${DCAT_VCPKG_ROOT}/vcpkg.exe")
  execute_process(
    COMMAND "${DCAT_VCPKG_ROOT}/bootstrap-vcpkg.bat" -disableMetrics
    WORKING_DIRECTORY "${DCAT_VCPKG_ROOT}"
    RESULT_VARIABLE bootstrap_result
  )
  if(NOT bootstrap_result EQUAL 0)
    message(FATAL_ERROR "Failed to bootstrap vcpkg")
  endif()
endif()

set(_dcat_vips_port "")
if(EXISTS "${DCAT_VCPKG_ROOT}/ports/vips")
  set(_dcat_vips_port "vips")
elseif(EXISTS "${DCAT_VCPKG_ROOT}/ports/libvips")
  set(_dcat_vips_port "libvips")
endif()

set(_dcat_vcpkg_packages ${DCAT_VCPKG_PACKAGES})
if(_dcat_vips_port)
  list(APPEND _dcat_vcpkg_packages "${_dcat_vips_port}")
endif()

set(_bundle_vips_script "")
if(NOT _dcat_vips_port)
  if(DCAT_BUNDLE_VIPS)
    message(STATUS
      "No vips/libvips port found in this vcpkg revision; "
      "cmake.deps will download libvips ${DCAT_VIPS_VERSION} prebuilt binaries.")
    set(_bundle_vips_script "${CMAKE_CURRENT_BINARY_DIR}/bundle-vips.cmake")
    file(WRITE "${_bundle_vips_script}" [=[
set(_vips_url "https://github.com/libvips/build-win64-mxe/releases/download/v@DCAT_VIPS_VERSION@/vips-dev-x64-all-@DCAT_VIPS_VERSION@.zip")
set(_vips_sha "@DCAT_VIPS_X64_ALL_SHA256@")
set(_archive_path "@CMAKE_BINARY_DIR@/downloads/vips-dev-x64-all-@DCAT_VIPS_VERSION@.zip")
set(_extract_dir "@CMAKE_BINARY_DIR@/downloads/vips-dev-x64-all-@DCAT_VIPS_VERSION@")
set(_prefix "@DEPS_INSTALL_DIR@/@DCAT_VCPKG_TRIPLET@")

if(NOT EXISTS "${_prefix}/include/vips/vips.h")
  file(MAKE_DIRECTORY "@CMAKE_BINARY_DIR@/downloads")
  file(DOWNLOAD "${_vips_url}" "${_archive_path}" EXPECTED_HASH "SHA256=${_vips_sha}" SHOW_PROGRESS STATUS _dl_status)
  list(GET _dl_status 0 _dl_code)
  if(NOT _dl_code EQUAL 0)
    message(FATAL_ERROR "Failed to download libvips archive from ${_vips_url}")
  endif()

  file(REMOVE_RECURSE "${_extract_dir}")
  file(MAKE_DIRECTORY "${_extract_dir}")
  file(ARCHIVE_EXTRACT INPUT "${_archive_path}" DESTINATION "${_extract_dir}")

  file(GLOB_RECURSE _vips_headers "${_extract_dir}/*/include/vips/vips.h")
  if(NOT _vips_headers)
    message(FATAL_ERROR "Could not locate include/vips/vips.h after extracting ${_archive_path}")
  endif()

  list(GET _vips_headers 0 _header)
  get_filename_component(_vips_include_dir "${_header}" DIRECTORY)
  get_filename_component(_include_dir "${_vips_include_dir}" DIRECTORY)
  get_filename_component(_root_dir "${_include_dir}" DIRECTORY)

  file(MAKE_DIRECTORY "${_prefix}/include" "${_prefix}/lib" "${_prefix}/bin")
  file(COPY "${_include_dir}/" DESTINATION "${_prefix}/include")
  if(EXISTS "${_root_dir}/lib")
    file(COPY "${_root_dir}/lib/" DESTINATION "${_prefix}/lib")
  endif()
  if(EXISTS "${_root_dir}/bin")
    file(COPY "${_root_dir}/bin/" DESTINATION "${_prefix}/bin")
  endif()
endif()
]=])
    string(REPLACE "\\" "/" _cmake_binary_dir "${CMAKE_BINARY_DIR}")
    string(REPLACE "\\" "/" _deps_install_dir "${DEPS_INSTALL_DIR}")
    file(READ "${_bundle_vips_script}" _bundle_vips_content)
    string(REPLACE "@DCAT_VIPS_VERSION@" "${DCAT_VIPS_VERSION}" _bundle_vips_content "${_bundle_vips_content}")
    string(REPLACE "@DCAT_VIPS_X64_ALL_SHA256@" "${DCAT_VIPS_X64_ALL_SHA256}" _bundle_vips_content "${_bundle_vips_content}")
    string(REPLACE "@CMAKE_BINARY_DIR@" "${_cmake_binary_dir}" _bundle_vips_content "${_bundle_vips_content}")
    string(REPLACE "@DEPS_INSTALL_DIR@" "${_deps_install_dir}" _bundle_vips_content "${_bundle_vips_content}")
    string(REPLACE "@DCAT_VCPKG_TRIPLET@" "${DCAT_VCPKG_TRIPLET}" _bundle_vips_content "${_bundle_vips_content}")
    file(WRITE "${_bundle_vips_script}" "${_bundle_vips_content}")
  else()
    message(WARNING
      "No vips/libvips port found and DCAT_BUNDLE_VIPS=OFF. "
      "You must provide libvips headers/libs separately.")
  endif()
endif()

set(_deps_post_commands "")
if(_bundle_vips_script)
  list(APPEND _deps_post_commands COMMAND "${CMAKE_COMMAND}" -P "${_bundle_vips_script}")
endif()

file(MAKE_DIRECTORY "${DEPS_INSTALL_DIR}")
file(
  WRITE
  "${CMAKE_BINARY_DIR}/deps-prefix.cmake"
  "set(DEPS_PREFIX \"${DEPS_INSTALL_DIR}/${DCAT_VCPKG_TRIPLET}\")\n"
)

add_custom_target(deps ALL
  COMMAND
    "${DCAT_VCPKG_ROOT}/vcpkg.exe"
    install
    --triplet
    "${DCAT_VCPKG_TRIPLET}"
    --x-install-root="${DEPS_INSTALL_DIR}"
    ${_dcat_vcpkg_packages}
  ${_deps_post_commands}
  COMMAND
    "${CMAKE_COMMAND}" -E echo "Dependency prefix: ${DEPS_INSTALL_DIR}/${DCAT_VCPKG_TRIPLET}"
  COMMAND
    "${CMAKE_COMMAND}" -E echo "Configure root project with -DDEPS_PREFIX=${DEPS_INSTALL_DIR}/${DCAT_VCPKG_TRIPLET}"
  USES_TERMINAL
)
