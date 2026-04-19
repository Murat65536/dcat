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
set(
  DCAT_VCPKG_PACKAGES
  "assimp;cglm;vips;pkgconf;shaderc;vulkan"
  CACHE STRING
  "Semicolon-separated vcpkg package list"
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
    ${DCAT_VCPKG_PACKAGES}
  COMMAND
    "${CMAKE_COMMAND}" -E echo "Dependency prefix: ${DEPS_INSTALL_DIR}/${DCAT_VCPKG_TRIPLET}"
  COMMAND
    "${CMAKE_COMMAND}" -E echo "Configure root project with -DDEPS_PREFIX=${DEPS_INSTALL_DIR}/${DCAT_VCPKG_TRIPLET}"
  USES_TERMINAL
)
