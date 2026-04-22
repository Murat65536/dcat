cmake_minimum_required(VERSION 3.21)
project(dcat VERSION 0.1.0 LANGUAGES C CXX)

include(CheckCCompilerFlag)
include(GNUInstallDirs)

option(USE_SANITIZERS "Enable AddressSanitizer and UBSan in debug builds" ON)
option(PROFILE "Enable gprof profiling flags (-pg)" OFF)
option(USE_BUNDLED "Use local dependencies from cmake.deps output" ON)
set(DEPS_PREFIX "" CACHE PATH "Dependency prefix (for example .deps/vcpkg_installed/x64-windows)")
set(DCAT_VCPKG_TRIPLET "x64-windows" CACHE STRING "vcpkg triplet used by cmake.deps")

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(WIN32 AND CMAKE_C_COMPILER)
  # When using CLion's bundled CMake with MSYS2, it doesn't automatically know to search 
  # inside the MSYS2 environment (e.g. C:/msys64/clang64). We tell it to look at the 
  # parent directory of the compiler (the sysroot).
  get_filename_component(_compiler_bin "${CMAKE_C_COMPILER}" DIRECTORY)
  get_filename_component(_sysroot "${_compiler_bin}" DIRECTORY)
  list(APPEND CMAKE_PREFIX_PATH "${_sysroot}")
endif()

if(WIN32 AND NOT DEFINED PKG_CONFIG_EXECUTABLE)
  find_program(PKG_CONFIG_EXECUTABLE NAMES pkg-config pkgconf
    HINTS
      "C:/msys64/clang64/bin"
      "C:/msys64/mingw64/bin"
      "C:/msys64/usr/bin"
  )
endif()

if(USE_BUNDLED AND NOT DEPS_PREFIX)
  if(WIN32)
    set(_default_deps_prefix "${CMAKE_SOURCE_DIR}/.deps/vcpkg_installed/${DCAT_VCPKG_TRIPLET}")
    set(_legacy_deps_prefix "${CMAKE_SOURCE_DIR}/.deps/vcpkg/${DCAT_VCPKG_TRIPLET}")
    if(EXISTS "${_default_deps_prefix}")
      set(DEPS_PREFIX "${_default_deps_prefix}")
    elseif(EXISTS "${_legacy_deps_prefix}")
      set(DEPS_PREFIX "${_legacy_deps_prefix}")
    else()
      message(FATAL_ERROR
        "Bundled dependencies are enabled but not found at:\n"
        "  ${_default_deps_prefix}\n"
        "Run:\n"
        "  cmake -S cmake.deps -B .deps -G Ninja -D CMAKE_BUILD_TYPE=Release\n"
        "  cmake --build .deps --config Release\n"
        "Or pass -DDEPS_PREFIX=<path>.")
    endif()
  else()
    message(FATAL_ERROR
      "Bundled dependencies are currently wired for Windows/MSVC first.\n"
      "For Linux/macOS configure with -DUSE_BUNDLED=OFF.")
  endif()
endif()

if(DEPS_PREFIX)
  list(PREPEND CMAKE_PREFIX_PATH "${DEPS_PREFIX}")

  set(_pkg_paths
    "${DEPS_PREFIX}/lib/pkgconfig"
    "${DEPS_PREFIX}/lib64/pkgconfig"
    "${DEPS_PREFIX}/share/pkgconfig"
  )
  if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
    list(APPEND _pkg_paths "$ENV{PKG_CONFIG_PATH}")
  endif()
  list(JOIN _pkg_paths ";" _pkg_path_joined)
  set(ENV{PKG_CONFIG_PATH} "${_pkg_path_joined}")

  if(NOT DEFINED PKG_CONFIG_EXECUTABLE OR NOT EXISTS "${PKG_CONFIG_EXECUTABLE}")
    find_program(DCAT_PKGCONF_EXECUTABLE
      NAMES pkgconf pkg-config
      HINTS
        "${DEPS_PREFIX}/tools/pkgconf"
        "${DEPS_PREFIX}/bin"
        "${DEPS_PREFIX}/tools"
      NO_DEFAULT_PATH
    )
    if(DCAT_PKGCONF_EXECUTABLE)
      set(PKG_CONFIG_EXECUTABLE "${DCAT_PKGCONF_EXECUTABLE}" CACHE FILEPATH "pkg-config executable" FORCE)
    endif()
  endif()
endif()

set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH ON)
find_package(PkgConfig QUIET)
find_package(Threads REQUIRED)

set(vulkan_target "")
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
  set(vulkan_target Vulkan::Vulkan)
elseif(PkgConfig_FOUND)
  pkg_check_modules(VULKAN QUIET IMPORTED_TARGET vulkan)
  if(TARGET PkgConfig::VULKAN)
    set(vulkan_target PkgConfig::VULKAN)
  endif()
endif()
if(NOT vulkan_target)
  find_path(VULKAN_INCLUDE_DIR NAMES vulkan/vulkan.h REQUIRED)
  find_library(VULKAN_LIBRARY NAMES vulkan-1 vulkan REQUIRED)
  add_library(dcat_vulkan INTERFACE)
  target_include_directories(dcat_vulkan INTERFACE "${VULKAN_INCLUDE_DIR}")
  target_link_libraries(dcat_vulkan INTERFACE "${VULKAN_LIBRARY}")
  set(vulkan_target dcat_vulkan)
endif()

set(assimp_target "")
find_package(assimp CONFIG QUIET)
if(TARGET assimp::assimp)
  set(assimp_target assimp::assimp)
elseif(PkgConfig_FOUND)
  pkg_check_modules(ASSIMP QUIET IMPORTED_TARGET assimp)
  if(TARGET PkgConfig::ASSIMP)
    set(assimp_target PkgConfig::ASSIMP)
  endif()
endif()
if(NOT assimp_target)
  find_path(ASSIMP_INCLUDE_DIR NAMES assimp/scene.h REQUIRED)
  find_library(ASSIMP_LIBRARY NAMES assimp assimp-vc143-mt assimp-vc142-mt REQUIRED)
  add_library(dcat_assimp INTERFACE)
  target_include_directories(dcat_assimp INTERFACE "${ASSIMP_INCLUDE_DIR}")
  target_link_libraries(dcat_assimp INTERFACE "${ASSIMP_LIBRARY}")
  set(assimp_target dcat_assimp)
endif()

set(cglm_target "")
if(PkgConfig_FOUND)
  pkg_check_modules(CGLM QUIET IMPORTED_TARGET cglm)
  if(TARGET PkgConfig::CGLM)
    set(cglm_target PkgConfig::CGLM)
  endif()
endif()
if(NOT cglm_target)
  find_path(CGLM_INCLUDE_DIR NAMES cglm/cglm.h REQUIRED)
  add_library(dcat_cglm INTERFACE)
  target_include_directories(dcat_cglm INTERFACE "${CGLM_INCLUDE_DIR}")
  set(cglm_target dcat_cglm)
endif()

set(vips_target "")
if(PkgConfig_FOUND AND NOT WIN32)
  pkg_check_modules(VIPS QUIET IMPORTED_TARGET vips)
  if(TARGET PkgConfig::VIPS)
    set(vips_target PkgConfig::VIPS)
  endif()
endif()
if(NOT vips_target)
  find_package(vips CONFIG QUIET)
  find_package(unofficial-vips CONFIG QUIET)
  if(TARGET unofficial::vips::vips)
    set(vips_target unofficial::vips::vips)
  elseif(TARGET vips::vips)
    set(vips_target vips::vips)
  else()
    find_path(VIPS_INCLUDE_DIR NAMES vips/vips.h REQUIRED)
    find_library(VIPS_LIBRARY NAMES vips libvips libvips-42 vips42 REQUIRED)
    set(_vips_extra_libs "")
    add_library(dcat_vips INTERFACE)
    target_include_directories(dcat_vips INTERFACE "${VIPS_INCLUDE_DIR}")
    if(DEPS_PREFIX)
      set(_glib_include_dir "${DEPS_PREFIX}/include/glib-2.0")
      set(_glib_config_include_dir "${DEPS_PREFIX}/lib/glib-2.0/include")
      if(EXISTS "${_glib_include_dir}")
        target_include_directories(dcat_vips INTERFACE "${_glib_include_dir}")
      endif()
      if(EXISTS "${_glib_config_include_dir}")
        target_include_directories(dcat_vips INTERFACE "${_glib_config_include_dir}")
      endif()
    endif()
    if(WIN32)
      find_library(GLIB2_LIBRARY NAMES glib-2.0 libglib-2.0 REQUIRED)
      find_library(GOBJECT2_LIBRARY NAMES gobject-2.0 libgobject-2.0 REQUIRED)
      list(APPEND _vips_extra_libs "${GLIB2_LIBRARY}" "${GOBJECT2_LIBRARY}")

      find_library(GIO2_LIBRARY NAMES gio-2.0 libgio-2.0)
      if(GIO2_LIBRARY)
        list(APPEND _vips_extra_libs "${GIO2_LIBRARY}")
      endif()
      find_library(GMODULE2_LIBRARY NAMES gmodule-2.0 libgmodule-2.0)
      if(GMODULE2_LIBRARY)
        list(APPEND _vips_extra_libs "${GMODULE2_LIBRARY}")
      endif()
    endif()
    target_link_libraries(dcat_vips INTERFACE "${VIPS_LIBRARY}" ${_vips_extra_libs})
    set(vips_target dcat_vips)
  endif()
endif()

set(extra_platform_libs "")
set(sixel_target "")
if(NOT WIN32)
  if(PkgConfig_FOUND)
    pkg_check_modules(SIXEL QUIET IMPORTED_TARGET libsixel)
    if(TARGET PkgConfig::SIXEL)
      set(sixel_target PkgConfig::SIXEL)
    endif()
  endif()
  if(NOT sixel_target)
    find_path(SIXEL_INCLUDE_DIR NAMES sixel.h REQUIRED)
    find_library(SIXEL_LIBRARY NAMES sixel REQUIRED)
    add_library(dcat_sixel INTERFACE)
    target_include_directories(dcat_sixel INTERFACE "${SIXEL_INCLUDE_DIR}")
    target_link_libraries(dcat_sixel INTERFACE "${SIXEL_LIBRARY}")
    set(sixel_target dcat_sixel)
  endif()

  find_library(M_LIBRARY NAMES m)
  if(M_LIBRARY)
    list(APPEND extra_platform_libs "${M_LIBRARY}")
  endif()

  find_library(RT_LIBRARY NAMES rt)
  if(RT_LIBRARY)
    list(APPEND extra_platform_libs "${RT_LIBRARY}")
  endif()
endif()

set(poly2tri_library "")
find_library(POLY2TRI_LIBRARY NAMES poly2tri)
if(POLY2TRI_LIBRARY)
  set(poly2tri_library "${POLY2TRI_LIBRARY}")
endif()

set(dcat_sources
  src/main.c
  src/core/args.c
  src/graphics/camera.c
  src/graphics/model.c
  src/graphics/animation.c
  src/graphics/texture.c
  src/graphics/texture_loader.c
  src/graphics/skydome.c
  src/renderer/vulkan_renderer.c
  src/renderer/vk_device.c
  src/renderer/vk_memory.c
  src/renderer/vk_shader.c
  src/renderer/vk_pipeline.c
  src/renderer/vk_resources.c
  src/renderer/vk_upload.c
  src/input/input_handler.c
  src/terminal/terminal.c
  src/terminal/truecolor_characters.c
  src/terminal/palette_characters.c
  src/terminal/block_characters.c
  src/terminal/sixel.c
  src/terminal/kitty.c
  src/terminal/kitty_shm.c
)

add_executable(dcat ${dcat_sources})
set_target_properties(dcat PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(dcat PRIVATE "${CMAKE_SOURCE_DIR}/src")

if(MSVC)
  target_compile_options(dcat PRIVATE /W3)
else()
  target_compile_options(dcat PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MSVC)
  check_c_compiler_flag("/experimental:c11atomics" HAS_C11_ATOMICS_FLAG)
  if(HAS_C11_ATOMICS_FLAG)
    target_compile_options(dcat PRIVATE /experimental:c11atomics)
  endif()
endif()

if(NOT MSVC)
  foreach(release_flag -march=native -ffast-math -funroll-loops)
    check_c_compiler_flag("${release_flag}" HAS_RELEASE_FLAG)
    if(HAS_RELEASE_FLAG)
      if(CMAKE_CONFIGURATION_TYPES)
        target_compile_options(dcat PRIVATE "$<$<CONFIG:Release>:${release_flag}>")
      elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(dcat PRIVATE "${release_flag}")
      endif()
    endif()
  endforeach()
endif()

if(PROFILE AND NOT MSVC)
  target_compile_options(dcat PRIVATE -pg)
  target_link_options(dcat PRIVATE -pg)
endif()

if(USE_SANITIZERS)
  if(WIN32)
    message(STATUS "USE_SANITIZERS is enabled, but sanitizer flags are disabled on Windows.")
  elseif(NOT MSVC)
    if(CMAKE_CONFIGURATION_TYPES)
      target_compile_options(dcat PRIVATE "$<$<CONFIG:Debug>:-fsanitize=address,undefined>")
      target_link_options(dcat PRIVATE "$<$<CONFIG:Debug>:-fsanitize=address,undefined>")
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
      target_compile_options(dcat PRIVATE -fsanitize=address,undefined)
      target_link_options(dcat PRIVATE -fsanitize=address,undefined)
    endif()
  endif()
endif()

target_compile_definitions(dcat PRIVATE CGLM_FORCE_DEPTH_ZERO_TO_ONE)
if(CMAKE_CONFIGURATION_TYPES)
  target_compile_definitions(dcat PRIVATE "$<$<CONFIG:Debug>:DEBUG_BUILD=1>")
  target_compile_definitions(dcat PRIVATE "$<$<CONFIG:Debug>:VK_ENABLE_VALIDATION=1>")
  target_compile_definitions(dcat PRIVATE "$<$<OR:$<CONFIG:Release>,$<BOOL:${PROFILE}>>:NDEBUG=1>")
else()
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(dcat PRIVATE DEBUG_BUILD=1 VK_ENABLE_VALIDATION=1)
  endif()
  if(CMAKE_BUILD_TYPE STREQUAL "Release" OR PROFILE)
    target_compile_definitions(dcat PRIVATE NDEBUG=1)
  endif()
endif()

target_link_libraries(dcat PRIVATE
  "${vulkan_target}"
  "${assimp_target}"
  "${cglm_target}"
  "${vips_target}"
  Threads::Threads
)
if(sixel_target)
  target_link_libraries(dcat PRIVATE "${sixel_target}")
endif()
if(poly2tri_library)
  target_link_libraries(dcat PRIVATE "${poly2tri_library}")
endif()
if(extra_platform_libs)
  target_link_libraries(dcat PRIVATE ${extra_platform_libs})
endif()

set(_glslc_hints "")
if(DEPS_PREFIX)
  list(APPEND _glslc_hints
    "${DEPS_PREFIX}/tools/shaderc"
    "${DEPS_PREFIX}/bin"
  )
endif()
find_program(GLSLC NAMES glslc HINTS ${_glslc_hints} REQUIRED)
set(shader_sources
  shader.vert
  shader.frag
  skydome.vert
  skydome.frag
)

set(shader_outputs "")

foreach(shader IN LISTS shader_sources)
  set(shader_input "${CMAKE_SOURCE_DIR}/shaders/${shader}")
  set(shader_output "${CMAKE_CURRENT_BINARY_DIR}/${shader}.spv")
  add_custom_command(
    OUTPUT "${shader_output}"
    COMMAND "${GLSLC}" $<$<CONFIG:Debug>:-g> "${shader_input}" -o "${shader_output}"
    DEPENDS "${shader_input}"
    VERBATIM
  )
  list(APPEND shader_outputs "${shader_output}")
endforeach()

add_custom_target(dcat_shaders ALL
  COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:dcat>/shaders"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${shader_outputs} "$<TARGET_FILE_DIR:dcat>/shaders"
  DEPENDS ${shader_outputs}
)
add_dependencies(dcat dcat_shaders)

if(WIN32)
  set(_deps_runtime_dlls "")
  if(DEPS_PREFIX)
    file(GLOB _deps_runtime_dlls "${DEPS_PREFIX}/bin/*.dll")
  endif()

  add_custom_command(TARGET dcat POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      $<TARGET_RUNTIME_DLLS:dcat>
      "$<TARGET_FILE_DIR:dcat>"
    COMMAND_EXPAND_LISTS
  )

  if(_deps_runtime_dlls)
    add_custom_command(TARGET dcat POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        ${_deps_runtime_dlls}
        "$<TARGET_FILE_DIR:dcat>"
      COMMAND_EXPAND_LISTS
    )
  endif()
endif()

install(TARGETS dcat RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES ${shader_outputs} DESTINATION "${CMAKE_INSTALL_DATADIR}/dcat/shaders")