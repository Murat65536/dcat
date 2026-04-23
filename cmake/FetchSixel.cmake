include(FetchContent)

FetchContent_Declare(
  ext_libsixel
  GIT_REPOSITORY https://github.com/libsixel/libsixel.git
  GIT_TAG master
)
FetchContent_GetProperties(ext_libsixel)

if(NOT ext_libsixel_POPULATED)
  FetchContent_Populate(ext_libsixel)

  file(WRITE "${ext_libsixel_SOURCE_DIR}/config.h" "
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define _POSIX_C_SOURCE 199309L
")

  configure_file(
    "${ext_libsixel_SOURCE_DIR}/include/sixel.h.in"
    "${ext_libsixel_SOURCE_DIR}/include/sixel.h"
    @ONLY
  )

  file(GLOB SIXEL_SRCS
    "${ext_libsixel_SOURCE_DIR}/src/allocator.c"
    "${ext_libsixel_SOURCE_DIR}/src/chunk.c"
    "${ext_libsixel_SOURCE_DIR}/src/decoder.c"
    "${ext_libsixel_SOURCE_DIR}/src/dither.c"
    "${ext_libsixel_SOURCE_DIR}/src/encoder.c"
    "${ext_libsixel_SOURCE_DIR}/src/frame.c"
    "${ext_libsixel_SOURCE_DIR}/src/output.c"
    "${ext_libsixel_SOURCE_DIR}/src/pixelformat.c"
    "${ext_libsixel_SOURCE_DIR}/src/quant.c"
    "${ext_libsixel_SOURCE_DIR}/src/scale.c"
    "${ext_libsixel_SOURCE_DIR}/src/status.c"
    "${ext_libsixel_SOURCE_DIR}/src/tty.c"
    "${ext_libsixel_SOURCE_DIR}/src/writer.c"
  )

  add_library(dcat_sixel STATIC ${SIXEL_SRCS})
  target_include_directories(dcat_sixel PUBLIC 
    "${ext_libsixel_SOURCE_DIR}/include"
  )
  target_include_directories(dcat_sixel PRIVATE
    "${ext_libsixel_SOURCE_DIR}"
    "${ext_libsixel_SOURCE_DIR}/src"
  )

  if(WIN32)
    target_compile_definitions(dcat_sixel PRIVATE _CRT_SECURE_NO_WARNINGS _POSIX_C_SOURCE=199309L)
  else()
    target_compile_definitions(dcat_sixel PRIVATE _POSIX_C_SOURCE=199309L)
  endif()
endif()
