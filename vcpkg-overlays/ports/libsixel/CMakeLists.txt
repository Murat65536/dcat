cmake_minimum_required(VERSION 3.21)
project(libsixel LANGUAGES C)

include(CheckIncludeFile)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

check_include_file("setjmp.h" HAVE_SETJMP_H)
check_include_file("sys/ioctl.h" HAVE_SYS_IOCTL_H)
check_include_file("sys/select.h" HAVE_SYS_SELECT_H)
check_include_file("sys/signal.h" HAVE_SYS_SIGNAL_H)
check_include_file("sys/types.h" HAVE_SYS_TYPES_H)
check_include_file("termios.h" HAVE_TERMIOS_H)
check_include_file("unistd.h" HAVE_UNISTD_H)

set(PACKAGE_VERSION "${UPSTREAM_VERSION}")
set(PACKAGE_DESCRIPTION "A SIXEL encoder implementation")
set(LS_LTVERSION "1:0:0")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/config.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/config.h")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/include/sixel.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/sixel.h" @ONLY)

set(libsixel_sources
  src/allocator.c
  src/chunk.c
  src/decoder.c
  src/dither.c
  src/encoder.c
  src/frame.c
  src/output.c
  src/pixelformat.c
  src/quant.c
  src/scale.c
  src/status.c
  src/writer.c
)

if(WIN32)
  list(APPEND libsixel_sources tty_win32.c)
else()
  list(APPEND libsixel_sources src/tty.c)
endif()

add_library(libsixel ${libsixel_sources})
target_include_directories(libsixel
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(WIN32)
  target_compile_definitions(libsixel PRIVATE _CRT_SECURE_NO_WARNINGS _POSIX_C_SOURCE=199309L)
else()
  target_compile_definitions(libsixel PRIVATE _POSIX_C_SOURCE=199309L)
endif()

set_target_properties(libsixel PROPERTIES OUTPUT_NAME sixel)

install(TARGETS libsixel EXPORT unofficial-libsixel-targets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/sixel.h" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/unofficial-libsixel-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/unofficial-libsixel-config.cmake"
  INSTALL_DESTINATION "share/unofficial-libsixel"
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/unofficial-libsixel-config-version.cmake"
  VERSION "${PACKAGE_VERSION}"
  COMPATIBILITY SameMajorVersion
)

install(EXPORT unofficial-libsixel-targets
  NAMESPACE unofficial::libsixel::
  FILE unofficial-libsixel-targets.cmake
  DESTINATION "share/unofficial-libsixel"
)
install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/unofficial-libsixel-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/unofficial-libsixel-config-version.cmake"
  DESTINATION "share/unofficial-libsixel"
)
