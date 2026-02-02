cmake_minimum_required(VERSION 3.16)
project(dcat VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set default build type to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Find required packages
find_package(Vulkan REQUIRED)
find_package(assimp REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SIXEL REQUIRED libsixel)

include(FetchContent)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    cglm
    GIT_REPOSITORY https://github.com/recp/cglm.git
    GIT_TAG v0.9.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb cglm)

if(NOT TARGET stb)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})
endif()

# Add executable
add_executable(dcat
    src/main.c
    src/camera.c
    src/model.c
    src/animation.c
    src/texture.c
    src/vulkan_renderer.c
    src/terminal.c
    src/input_device.c
    src/skydome.c
)

# cglm uses SIMD with aligned stores - we need unaligned for malloc'd memory
target_include_directories(dcat PRIVATE
    ${Vulkan_INCLUDE_DIRS}
    ${SIXEL_INCLUDE_DIRS}
    src
)

target_link_libraries(dcat PRIVATE
    Vulkan::Vulkan
    assimp
    cglm
    ${SIXEL_LIBRARIES}
    stb
    rt
    pthread
    m
)

# Debug build configuration
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Configuring Debug build with validation layers")
    target_compile_definitions(dcat PRIVATE 
        DEBUG_BUILD=1
        VK_ENABLE_VALIDATION=1
    )
    target_compile_options(dcat PRIVATE 
        -g
        -O0
        -Wall
        -Wextra
        -Wpedantic
        -fsanitize=address
        -fsanitize=undefined
    )
    target_link_options(dcat PRIVATE
        -fsanitize=address
        -fsanitize=undefined
    )
endif()

# Release build optimization
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Configuring Release build with optimizations")
    target_compile_definitions(dcat PRIVATE NDEBUG=1)
    target_compile_options(dcat PRIVATE 
        -O3 
        -march=native
        -g
        -DNDEBUG
    )
    set_target_properties(dcat PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION TRUE
    )
endif()

# RelWithDebInfo build
if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    message(STATUS "Configuring RelWithDebInfo build")
    target_compile_options(dcat PRIVATE -O2 -g)
endif()

# Compile shaders
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found!")
endif()

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

set(SHADERS
    ${SHADER_SOURCE_DIR}/shader.vert
    ${SHADER_SOURCE_DIR}/shader.frag
    ${SHADER_SOURCE_DIR}/skydome.vert
    ${SHADER_SOURCE_DIR}/skydome.frag
)

foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_OUTPUT ${SHADER_BINARY_DIR}/${SHADER_NAME}.spv)
    
    # Add debug info to shaders in debug builds
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(
            OUTPUT ${SPIRV_OUTPUT}
            COMMAND ${GLSLC} -g ${SHADER} -o ${SPIRV_OUTPUT}
            DEPENDS ${SHADER}
            COMMENT "Compiling ${SHADER_NAME} with debug info"
        )
    else()
        add_custom_command(
            OUTPUT ${SPIRV_OUTPUT}
            COMMAND ${GLSLC} ${SHADER} -o ${SPIRV_OUTPUT}
            DEPENDS ${SHADER}
            COMMENT "Compiling ${SHADER_NAME}"
        )
    endif()
    
    list(APPEND SPIRV_SHADERS ${SPIRV_OUTPUT})
endforeach()

add_custom_target(shaders DEPENDS ${SPIRV_SHADERS})
add_dependencies(dcat shaders)

# Install
install(TARGETS dcat RUNTIME DESTINATION bin)
install(DIRECTORY ${SHADER_BINARY_DIR}/ DESTINATION share/dcat/shaders)
