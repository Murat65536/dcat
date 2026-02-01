cmake_minimum_required(VERSION 3.16)
project(dcat VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(Vulkan REQUIRED)
find_package(assimp REQUIRED)
find_package(glm REQUIRED)
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
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb VulkanMemoryAllocator)

# stb doesn't have a CMakeLists.txt, so we define the target manually if it wasn't created
if(NOT TARGET stb)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})
endif()

# Add executable
add_executable(dcat
    src/main.cpp
    src/camera.cpp
    src/model.cpp
    src/animation.cpp
    src/texture.cpp
    src/vulkan_renderer.cpp
    src/terminal.cpp
    src/input_device.cpp
)

target_include_directories(dcat PRIVATE
    ${Vulkan_INCLUDE_DIRS}
    ${SIXEL_INCLUDE_DIRS}
    src
)

target_link_libraries(dcat PRIVATE
    Vulkan::Vulkan
    assimp
    glm::glm
    ${SIXEL_LIBRARIES}
    stb
    rt
    VulkanMemoryAllocator
)

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
)

foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_OUTPUT ${SHADER_BINARY_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${GLSLC} ${SHADER} -o ${SPIRV_OUTPUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${SHADER_NAME}"
    )
    list(APPEND SPIRV_SHADERS ${SPIRV_OUTPUT})
endforeach()

add_custom_target(shaders DEPENDS ${SPIRV_SHADERS})
add_dependencies(dcat shaders)

# Install
install(TARGETS dcat RUNTIME DESTINATION bin)
install(DIRECTORY ${SHADER_BINARY_DIR}/ DESTINATION share/dcat/shaders)

# Release build optimization
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(dcat PRIVATE -O3 -march=native)
    set_target_properties(dcat PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION TRUE
    )
endif()
